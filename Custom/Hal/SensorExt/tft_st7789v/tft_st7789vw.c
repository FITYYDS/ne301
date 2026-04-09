#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include "main.h"
#include "spi.h"
#include "debug.h"
#include "tft_st7789vw.h"
#include "common_utils.h"
#include "cmsis_os2.h"

/* ==================== Local configuration ==================== */

#define TFT_SPI_TIMEOUT_MS        1000U
/* Use DMA for data blocks >= this size (bytes). */
#define TFT_DMA_THRESHOLD         64U

/* ST7789 commands */
#define ST7789_CMD_SWRESET        0x01U
#define ST7789_CMD_SLPIN          0x10U
#define ST7789_CMD_SLPOUT         0x11U
#define ST7789_CMD_INVOFF         0x20U
#define ST7789_CMD_INVON          0x21U
#define ST7789_CMD_GAMSET         0x26U
#define ST7789_CMD_DISPON         0x29U
#define ST7789_CMD_CASET          0x2AU
#define ST7789_CMD_RASET          0x2BU
#define ST7789_CMD_RAMWR          0x2CU
#define ST7789_CMD_MADCTL         0x36U
#define ST7789_CMD_COLMOD         0x3AU

/* MADCTL bits */
#define ST7789_MADCTL_MY          0x80U
#define ST7789_MADCTL_MX          0x40U
#define ST7789_MADCTL_MV          0x20U
#define ST7789_MADCTL_RGB         0x00U  /* 0x00 = BGR, 0x08 = RGB, depends on panel */

/* ==================== Driver context ==================== */

static tft_st7789vw_handle_t s_tft = {
    .width = TFT_ST7789VW_WIDTH,
    .height = TFT_ST7789VW_HEIGHT,
    .orientation = TFT_ORIENTATION_PORTRAIT_0,
    .initialized = false,
};

static bool s_hw_initialized = false;

/* Maximum logical width used for static line buffer. */
#define TFT_ST7789VW_MAX_WIDTH    TFT_ST7789VW_WIDTH

/* Line buffer in big-endian byte order (high byte first) for ST7789.
 * Placed in uncached region so DMA sees CPU-written data without cache coherency. */
static uint8_t s_line_bytes[2U * TFT_ST7789VW_MAX_WIDTH] ALIGN_32 UNCACHED;

/* ==================== Low level helpers (hardware SPI6) ==================== */

/* CS (NSS) is driven by hardware SPI6. D/C pin: command = low, data = high. */
#ifdef TFT_DC_Pin
#define TFT_DC_COMMAND()  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET)
#define TFT_DC_DATA()     HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET)
#else
static bool s_dc_pin_reported = false;
#define TFT_DC_COMMAND()  do {                                              \
        if (!s_dc_pin_reported) {                                           \
            LOG_DRV_ERROR("ST7789VW: TFT_DC_Pin not defined.");         \
            s_dc_pin_reported = true;                                       \
        }                                                                   \
    } while (0)
#define TFT_DC_DATA()    TFT_DC_COMMAND()
#endif

static void tft_hw_reset(void)
{
#ifdef TFT_RST_Pin
    HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_RESET);
    osDelay(10);
    HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_SET);
    osDelay(120);
#else
    osDelay(150);
#endif
}

static void tft_hw_backlight_on(void)
{
#ifdef TFT_BL_Pin
    HAL_GPIO_WritePin(TFT_BL_GPIO_Port, TFT_BL_Pin, GPIO_PIN_SET);
#endif
}

static void tft_hw_backlight_off(void)
{
#ifdef TFT_BL_Pin
    HAL_GPIO_WritePin(TFT_BL_GPIO_Port, TFT_BL_Pin, GPIO_PIN_RESET);
#endif
}

/* Blocking TX via hardware SPI6 (CS controlled by peripheral). */
static int tft_spi6_write(const uint8_t *data, uint32_t len)
{
    if (len == 0U) {
        return 0;
    }
    return SPI6_WriteBytes(data, len, TFT_SPI_TIMEOUT_MS);
}

/* DMA TX for larger blocks; CS and D/C are not changed here (caller sets D/C). */
static int tft_spi6_write_dma(const uint8_t *data, uint32_t len)
{
    if (len == 0U || len > 0xFFFFU) {
        return (len == 0U) ? 0 : -1;
    }
    return SPI6_WriteBytesDMA(data, len, TFT_SPI_TIMEOUT_MS);
}

static int tft_write_command(uint8_t cmd)
{
    TFT_DC_COMMAND();
    return tft_spi6_write(&cmd, 1U);
}

static int tft_write_data8(uint8_t data)
{
    TFT_DC_DATA();
    return tft_spi6_write(&data, 1U);
}

static int tft_write_data(const uint8_t *data, uint32_t len)
{
    TFT_DC_DATA();
    if (len >= TFT_DMA_THRESHOLD) {
        return tft_spi6_write_dma(data, len);
    }
    return tft_spi6_write(data, len);
}

static int tft_write_data16(uint16_t value)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(value >> 8);
    buf[1] = (uint8_t)(value & 0xFFU);
    return tft_write_data(buf, sizeof(buf));
}

/* Set address window (x0,y0) .. (x1,y1) inclusive. */
static int tft_set_address_window(uint16_t x0, uint16_t y0,
                                  uint16_t x1, uint16_t y1)
{
    uint8_t data[4];
    int ret;

    if ((x1 < x0) || (y1 < y0)) {
        return -1;
    }

    ret = tft_write_command(ST7789_CMD_CASET);
    if (ret != 0) return ret;
    data[0] = (uint8_t)(x0 >> 8);
    data[1] = (uint8_t)(x0 & 0xFFU);
    data[2] = (uint8_t)(x1 >> 8);
    data[3] = (uint8_t)(x1 & 0xFFU);
    ret = tft_write_data(data, sizeof(data));
    if (ret != 0) return ret;

    ret = tft_write_command(ST7789_CMD_RASET);
    if (ret != 0) return ret;
    data[0] = (uint8_t)(y0 >> 8);
    data[1] = (uint8_t)(y0 & 0xFFU);
    data[2] = (uint8_t)(y1 >> 8);
    data[3] = (uint8_t)(y1 & 0xFFU);
    ret = tft_write_data(data, sizeof(data));
    if (ret != 0) return ret;

    return tft_write_command(ST7789_CMD_RAMWR);
}

/* ==================== Initialization sequence ==================== */

static int tft_st7789vw_hw_init_sequence(void)
{
    int ret;

    tft_hw_reset();

    /* Software reset */
    ret = tft_write_command(ST7789_CMD_SWRESET);
    if (ret != 0) return ret;
    osDelay(150);

    /* Sleep out */
    ret = tft_write_command(ST7789_CMD_SLPOUT);
    if (ret != 0) return ret;
    osDelay(120);

    /* Column address set: 0 .. 239 */
    ret = tft_write_command(ST7789_CMD_CASET);
    if (ret != 0) return ret;
    ret = tft_write_data8(0x00); if (ret != 0) return ret;
    ret = tft_write_data8(0x00); if (ret != 0) return ret;
    ret = tft_write_data8(0x00); if (ret != 0) return ret;
    ret = tft_write_data8(0xEF); if (ret != 0) return ret;

    /* Row address set: 0 .. 239 */
    ret = tft_write_command(ST7789_CMD_RASET);
    if (ret != 0) return ret;
    ret = tft_write_data8(0x00); if (ret != 0) return ret;
    ret = tft_write_data8(0x00); if (ret != 0) return ret;
    ret = tft_write_data8(0x00); if (ret != 0) return ret;
    ret = tft_write_data8(0xEF); if (ret != 0) return ret;

    /* Porch control (same as example) */
    ret = tft_write_command(0xB2);
    if (ret != 0) return ret;
    {
        uint8_t porch[5] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
        ret = tft_write_data(porch, sizeof(porch));
        if (ret != 0) return ret;
    }

    /* Display inversion ON (match working TB154 example) */
    ret = tft_write_command(ST7789_CMD_INVON);
    if (ret != 0) return ret;

    /* Gate control */
    ret = tft_write_command(0xB7);
    if (ret != 0) return ret;
    ret = tft_write_data8(0x35);
    if (ret != 0) return ret;

    /* VCOM, power settings (based on example "修正版初始化") */
    ret = tft_write_command(0xBB); if (ret != 0) return ret;
    ret = tft_write_data8(0x19);  if (ret != 0) return ret;

    ret = tft_write_command(0xC0); if (ret != 0) return ret;
    ret = tft_write_data8(0x2C);  if (ret != 0) return ret;

    ret = tft_write_command(0xC2); if (ret != 0) return ret;
    ret = tft_write_data8(0x01);  if (ret != 0) return ret;

    ret = tft_write_command(0xC3); if (ret != 0) return ret;
    ret = tft_write_data8(0x12);  if (ret != 0) return ret;

    ret = tft_write_command(0xC4); if (ret != 0) return ret;
    ret = tft_write_data8(0x20);  if (ret != 0) return ret;

    ret = tft_write_command(0xC6); if (ret != 0) return ret;
    ret = tft_write_data8(0x0F);  if (ret != 0) return ret;

    ret = tft_write_command(0xD0); if (ret != 0) return ret;
    ret = tft_write_data8(0xA4);  if (ret != 0) return ret;
    ret = tft_write_data8(0xA1);  if (ret != 0) return ret;

    /* Gamma settings (copied from example) */
    {
        uint8_t gamma_pos[] = {
            0xD0, 0x08, 0x11, 0x08,
            0x0C, 0x15, 0x39, 0x33,
            0x50, 0x08, 0x13, 0x04,
            0x08, 0x00
        };
        uint8_t gamma_neg[] = {
            0xD0, 0x08, 0x11, 0x08,
            0x0C, 0x15, 0x39, 0x33,
            0x50, 0x08, 0x13, 0x04,
            0x08, 0x00
        };
        ret = tft_write_command(0xE0);
        if (ret != 0) return ret;
        ret = tft_write_data(gamma_pos, sizeof(gamma_pos));
        if (ret != 0) return ret;

        ret = tft_write_command(0xE1);
        if (ret != 0) return ret;
        ret = tft_write_data(gamma_neg, sizeof(gamma_neg));
        if (ret != 0) return ret;
    }

    /* Memory data access control (orientation set later) */
    ret = tft_write_command(ST7789_CMD_MADCTL);
    if (ret != 0) return ret;
    ret = tft_write_data8(ST7789_MADCTL_RGB);
    if (ret != 0) return ret;

    /* Pixel format = 16‑bit (RGB565) – CRITICAL */
    ret = tft_write_command(ST7789_CMD_COLMOD);
    if (ret != 0) return ret;
    /* Use 0x05 as in the fixed TB154 ST7789VW init sequence. */
    ret = tft_write_data8(0x05U);
    if (ret != 0) return ret;

    /* Display on */
    ret = tft_write_command(ST7789_CMD_DISPON);
    if (ret != 0) return ret;
    osDelay(100);

    return 0;
}

/* ==================== Public API ==================== */

int tft_st7789vw_init(void)
{
    if (s_tft.initialized) {
        return 0;
    }

    if (!s_hw_initialized) {
        MX_SPI6_Init();

#if defined(TFT_BL_Pin) || defined(TFT_RST_Pin) || defined(TFT_DC_Pin)
        GPIO_InitTypeDef GPIO_InitStruct = {0};

        __HAL_RCC_GPIOB_CLK_ENABLE();
        GPIO_InitStruct.Pin = 0;
#ifdef TFT_BL_Pin
        GPIO_InitStruct.Pin |= TFT_BL_Pin;
#endif
#ifdef TFT_RST_Pin
        GPIO_InitStruct.Pin |= TFT_RST_Pin;
#endif
#ifdef TFT_DC_Pin
        GPIO_InitStruct.Pin |= TFT_DC_Pin;
#endif
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

#ifdef TFT_BL_Pin
        HAL_GPIO_WritePin(TFT_BL_GPIO_Port, TFT_BL_Pin, GPIO_PIN_RESET);
#endif
#ifdef TFT_RST_Pin
        HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_SET);
#endif
#ifdef TFT_DC_Pin
        HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
#endif

#endif /* TFT_BL_Pin || TFT_RST_Pin || TFT_DC_Pin */

        s_hw_initialized = true;
    }

    LOG_DRV_INFO("ST7789VW: init start (SPI6)");

    if (tft_st7789vw_hw_init_sequence() != 0) {
        LOG_DRV_ERROR("ST7789VW: hardware init sequence failed");
        return -1;
    }

    tft_hw_backlight_on();

    s_tft.initialized = true;
    s_tft.width = TFT_ST7789VW_WIDTH;
    s_tft.height = TFT_ST7789VW_HEIGHT;
    s_tft.orientation = TFT_ORIENTATION_PORTRAIT_0;

    /* Simple power‑on test pattern: fill screen with red. */
    (void)tft_st7789vw_fill_screen(TFT_COLOR_RED);

    LOG_DRV_INFO("ST7789VW: init done, %ux%u",
                 (unsigned int)s_tft.width,
                 (unsigned int)s_tft.height);
    return 0;
}

void tft_st7789vw_deinit(void)
{
    if (!s_tft.initialized) {
        return;
    }

    /* Enter sleep and turn display off */
    if (tft_write_command(ST7789_CMD_SLPIN) == 0) {
        (void)tft_write_command(0x28U); /* DISPOFF, optional */
    }
    tft_hw_backlight_off();
    s_tft.initialized = false;
}

const tft_st7789vw_handle_t *tft_st7789vw_get_handle(void)
{
    return &s_tft;
}

static int tft_update_madctl_for_orientation(tft_orientation_t orientation)
{
    uint8_t madctl = ST7789_MADCTL_RGB;

    switch (orientation) {
    case TFT_ORIENTATION_PORTRAIT_0:
        madctl = ST7789_MADCTL_RGB | ST7789_MADCTL_MX;
        s_tft.width = TFT_ST7789VW_WIDTH;
        s_tft.height = TFT_ST7789VW_HEIGHT;
        break;
    case TFT_ORIENTATION_LANDSCAPE_90:
        madctl = ST7789_MADCTL_RGB | ST7789_MADCTL_MV;
        s_tft.width = TFT_ST7789VW_HEIGHT;
        s_tft.height = TFT_ST7789VW_WIDTH;
        break;
    case TFT_ORIENTATION_PORTRAIT_180:
        madctl = ST7789_MADCTL_RGB | ST7789_MADCTL_MY;
        s_tft.width = TFT_ST7789VW_WIDTH;
        s_tft.height = TFT_ST7789VW_HEIGHT;
        break;
    case TFT_ORIENTATION_LANDSCAPE_270:
        madctl = ST7789_MADCTL_RGB | ST7789_MADCTL_MX | ST7789_MADCTL_MY | ST7789_MADCTL_MV;
        s_tft.width = TFT_ST7789VW_HEIGHT;
        s_tft.height = TFT_ST7789VW_WIDTH;
        break;
    default:
        return -1;
    }

    if (tft_write_command(ST7789_CMD_MADCTL) != 0) {
        return -1;
    }
    if (tft_write_data8(madctl) != 0) {
        return -1;
    }
    return 0;
}

int tft_st7789vw_set_orientation(tft_orientation_t orientation)
{
    if (!s_tft.initialized) {
        return -1;
    }

    if (tft_update_madctl_for_orientation(orientation) != 0) {
        LOG_DRV_ERROR("ST7789VW: set orientation %d failed", orientation);
        return -1;
    }
    s_tft.orientation = orientation;
    return 0;
}

/* Helpers for clipping */
static bool tft_clip_rect(uint16_t *x, uint16_t *y,
                          uint16_t *w, uint16_t *h)
{
    if (*x >= s_tft.width || *y >= s_tft.height) {
        return false;
    }
    if ((*x + *w) > s_tft.width) {
        *w = (uint16_t)(s_tft.width - *x);
    }
    if ((*y + *h) > s_tft.height) {
        *h = (uint16_t)(s_tft.height - *y);
    }
    return (*w > 0U) && (*h > 0U);
}

int tft_st7789vw_fill_screen(uint16_t color)
{
    return tft_st7789vw_fill_rect(0U, 0U, s_tft.width, s_tft.height, color);
}

int tft_st7789vw_fill_rect(uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h,
                           uint16_t color)
{
    if (!s_tft.initialized) {
        return -1;
    }
    if (!tft_clip_rect(&x, &y, &w, &h)) {
        return 0;
    }

    if (w > TFT_ST7789VW_MAX_WIDTH) {
        w = TFT_ST7789VW_MAX_WIDTH;
    }

    if (tft_set_address_window(x, y,
                               (uint16_t)(x + w - 1U),
                               (uint16_t)(y + h - 1U)) != 0) {
        return -1;
    }

    uint32_t line_pixels = w;
    uint32_t line_bytes = line_pixels * 2U;

    /* ST7789 expects RGB565 high byte first; on LE host we must send big-endian. */
    for (uint32_t i = 0; i < line_pixels; ++i) {
        s_line_bytes[2U * i]     = (uint8_t)(color >> 8);
        s_line_bytes[2U * i + 1] = (uint8_t)(color & 0xFFU);
    }

    for (uint32_t row = 0; row < h; ++row) {
        if (tft_write_data(s_line_bytes, line_bytes) != 0) {
            return -1;
        }
    }
    return 0;
}

int tft_st7789vw_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (!s_tft.initialized) {
        return -1;
    }
    if (x >= s_tft.width || y >= s_tft.height) {
        return 0;
    }
    if (tft_set_address_window(x, y, x, y) != 0) {
        return -1;
    }
    return tft_write_data16(color);
}

int tft_st7789vw_draw_rect(uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h,
                           uint16_t color)
{
    if (w == 0U || h == 0U) {
        return 0;
    }
    int ret = 0;
    ret |= tft_st7789vw_draw_line((int16_t)x, (int16_t)y,
                                  (int16_t)(x + w - 1U), (int16_t)y, color);
    ret |= tft_st7789vw_draw_line((int16_t)x, (int16_t)(y + h - 1U),
                                  (int16_t)(x + w - 1U), (int16_t)(y + h - 1U), color);
    ret |= tft_st7789vw_draw_line((int16_t)x, (int16_t)y,
                                  (int16_t)x, (int16_t)(y + h - 1U), color);
    ret |= tft_st7789vw_draw_line((int16_t)(x + w - 1U), (int16_t)y,
                                  (int16_t)(x + w - 1U), (int16_t)(y + h - 1U), color);
    return ret;
}

int tft_st7789vw_draw_line(int16_t x0, int16_t y0,
                           int16_t x1, int16_t y1,
                           uint16_t color)
{
    if (!s_tft.initialized) {
        return -1;
    }

    int16_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (dx > dy ? dx : -dy) / 2;
    int16_t e2;

    for (;;) {
        (void)tft_st7789vw_draw_pixel((uint16_t)x0, (uint16_t)y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dy) {
            err += dx;
            y0 += sy;
        }
    }
    return 0;
}

int tft_st7789vw_draw_bitmap(uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h,
                             const uint16_t *pixels)
{
    if (!s_tft.initialized || pixels == NULL) {
        return -1;
    }
    if (!tft_clip_rect(&x, &y, &w, &h)) {
        return 0;
    }

    if (tft_set_address_window(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U)) != 0) {
        return -1;
    }

    /* Send each row in big-endian byte order (high byte first) for ST7789. */
    uint32_t line_bytes = (uint32_t)w * 2U;
    for (uint32_t row = 0; row < h; ++row) {
        const uint16_t *row_pixels = pixels + (uint32_t)row * w;
        for (uint32_t col = 0; col < w; ++col) {
            uint16_t p = row_pixels[col];
            s_line_bytes[2U * col]     = (uint8_t)(p >> 8);
            s_line_bytes[2U * col + 1] = (uint8_t)(p & 0xFFU);
        }
        if (tft_write_data(s_line_bytes, line_bytes) != 0) {
            return -1;
        }
    }
    return 0;
}

int tft_st7789vw_draw_bitmap_be(uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h,
                                const uint8_t *pixels_be)
{
    if (!s_tft.initialized || pixels_be == NULL) {
        return -1;
    }
    if (!tft_clip_rect(&x, &y, &w, &h)) {
        return 0;
    }

    if (tft_set_address_window(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U)) != 0) {
        return -1;
    }

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT != 0U)
    SCB_CleanDCache_by_Addr((void *)pixels_be, (int32_t)((uint32_t)w * (uint32_t)h * 2U));
#endif

    uint32_t line_bytes = (uint32_t)w * 2U;
    for (uint32_t row = 0; row < h; ++row) {
        if (tft_write_data(pixels_be + row * line_bytes, line_bytes) != 0) {
            return -1;
        }
    }
    return 0;
}

/* ==================== Simple 5x7 ASCII font ==================== */

/* 96 characters (0x20..0x7F), 5 bytes per char, from classic 5x7 font. */
static const uint8_t s_font5x7[96][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')' */
    {0x14,0x08,0x3E,0x08,0x14}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V' */
    {0x7F,0x20,0x18,0x20,0x7F}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* '\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 'f' */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 't' */
    {0x3C,0x40,0x40,0x20,0x7C}, /* 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* '}' */
    {0x10,0x08,0x08,0x10,0x08}, /* '~' */
};

/* Write one RGB565 pixel (big-endian) into an in-memory buffer. */
static void tft_st7789vw_draw_pixel_to_buffer(uint8_t *buf_be,
                                              uint16_t buf_w,
                                              uint16_t buf_h,
                                              uint16_t x,
                                              uint16_t y,
                                              uint16_t color)
{
    if (buf_be == NULL || x >= buf_w || y >= buf_h) {
        return;
    }

    /* buf_be is RGB565 big-endian: high byte, then low byte per pixel */
    uint32_t idx = ((uint32_t)y * (uint32_t)buf_w + (uint32_t)x) * 2U;
    buf_be[idx]     = (uint8_t)(color >> 8);
    buf_be[idx + 1] = (uint8_t)(color & 0xFFU);
}

int tft_st7789vw_draw_char(uint16_t x, uint16_t y, char c,
                           uint16_t fg_color, uint16_t bg_color,
                           uint8_t scale)
{
    if (!s_tft.initialized) {
        return -1;
    }
    if (scale == 0U) {
        scale = 1U;
    }
    if ((uint8_t)c < 0x20U || (uint8_t)c > 0x7FU) {
        c = '?';
    }

    const uint8_t *glyph = s_font5x7[(uint8_t)c - 0x20U];
    uint16_t char_w = (uint16_t)(5U * scale);
    uint16_t char_h = (uint16_t)(7U * scale);

    if (x >= s_tft.width || y >= s_tft.height) {
        return 0;
    }
    if ((x + char_w) > s_tft.width || (y + char_h) > s_tft.height) {
        /* Simple clipping: if completely out of bounds, skip. */
        if (x >= s_tft.width || y >= s_tft.height) {
            return 0;
        }
    }

    for (uint8_t col = 0; col < 5U; ++col) {
        uint8_t line = glyph[col];
        for (uint8_t row = 0; row < 7U; ++row) {
            uint16_t color = (line & 0x01U) ? fg_color : bg_color;
            if (color == bg_color) {
                /* Optional: skip drawing background pixels for better performance. */
                line >>= 1;
                continue;
            }
            for (uint8_t sx = 0; sx < scale; ++sx) {
                for (uint8_t sy = 0; sy < scale; ++sy) {
                    (void)tft_st7789vw_draw_pixel(
                        (uint16_t)(x + (uint16_t)col * scale + sx),
                        (uint16_t)(y + (uint16_t)row * scale + sy),
                        color);
                }
            }
            line >>= 1;
        }
    }
    return 0;
}

int tft_st7789vw_draw_char_to_buffer(uint8_t *buf_be,
                                     uint16_t buf_w,
                                     uint16_t buf_h,
                                     uint16_t x,
                                     uint16_t y,
                                     char c,
                                     uint16_t fg_color,
                                     uint16_t bg_color,
                                     uint8_t scale)
{
    if (buf_be == NULL) {
        return -1;
    }
    if (scale == 0U) {
        scale = 1U;
    }
    if ((uint8_t)c < 0x20U || (uint8_t)c > 0x7FU) {
        c = '?';
    }

    const uint8_t *glyph = s_font5x7[(uint8_t)c - 0x20U];
    uint16_t char_w = (uint16_t)(5U * scale);
    uint16_t char_h = (uint16_t)(7U * scale);

    if (x >= buf_w || y >= buf_h) {
        return 0;
    }
    if ((x + char_w) > buf_w || (y + char_h) > buf_h) {
        /* Simple clipping: if completely out of bounds, skip. */
        if (x >= buf_w || y >= buf_h) {
            return 0;
        }
    }

    for (uint8_t col = 0; col < 5U; ++col) {
        uint8_t line = glyph[col];
        for (uint8_t row = 0; row < 7U; ++row) {
            uint16_t color = (line & 0x01U) ? fg_color : bg_color;
            if (color == bg_color) {
                /* Skip background pixels to keep video content visible. */
                line >>= 1;
                continue;
            }
            for (uint8_t sx = 0; sx < scale; ++sx) {
                for (uint8_t sy = 0; sy < scale; ++sy) {
                    tft_st7789vw_draw_pixel_to_buffer(
                        buf_be,
                        buf_w,
                        buf_h,
                        (uint16_t)(x + (uint16_t)col * scale + sx),
                        (uint16_t)(y + (uint16_t)row * scale + sy),
                        color);
                }
            }
            line >>= 1;
        }
    }
    return 0;
}

int tft_st7789vw_draw_string(uint16_t x, uint16_t y, const char *str,
                             uint16_t fg_color, uint16_t bg_color,
                             uint8_t scale)
{
    if (!s_tft.initialized || str == NULL) {
        return -1;
    }
    uint16_t cursor_x = x;
    uint16_t cursor_y = y;
    uint16_t char_w = (uint16_t)(6U * scale); /* 5 pixels + 1 space */

    while (*str != '\0') {
        if (*str == '\n') {
            cursor_x = x;
            cursor_y = (uint16_t)(cursor_y + (uint16_t)8U * scale);
        } else {
            (void)tft_st7789vw_draw_char(cursor_x, cursor_y, *str,
                                         fg_color, bg_color, scale);
            cursor_x = (uint16_t)(cursor_x + char_w);
        }
        ++str;
    }
    return 0;
}

int tft_st7789vw_draw_string_to_buffer(uint8_t *buf_be,
                                       uint16_t buf_w,
                                       uint16_t buf_h,
                                       uint16_t x,
                                       uint16_t y,
                                       const char *str,
                                       uint16_t fg_color,
                                       uint16_t bg_color,
                                       uint8_t scale)
{
    if (buf_be == NULL || str == NULL) {
        return -1;
    }
    if (scale == 0U) {
        scale = 1U;
    }

    uint16_t cursor_x = x;
    uint16_t cursor_y = y;
    uint16_t char_w = (uint16_t)(6U * scale); /* 5 pixels + 1 space */

    while (*str != '\0') {
        if (*str == '\n') {
            cursor_x = x;
            cursor_y = (uint16_t)(cursor_y + (uint16_t)8U * scale);
        } else {
            (void)tft_st7789vw_draw_char_to_buffer(buf_be,
                                                   buf_w,
                                                   buf_h,
                                                   cursor_x,
                                                   cursor_y,
                                                   *str,
                                                   fg_color,
                                                   bg_color,
                                                   scale);
            cursor_x = (uint16_t)(cursor_x + char_w);
        }
        ++str;
    }
    return 0;
}

/* ==================== Debug command support ==================== */

#include "generic_cmdline.h"

static int tft_cmd(int argc, char **argv)
{
    if (argc < 2) {
        LOG_SIMPLE("Usage:");
        LOG_SIMPLE("  tft init");
        LOG_SIMPLE("  tft fill <color_hex>");
        LOG_SIMPLE("  tft text <x> <y> <string>");
        LOG_SIMPLE("  tft pins 0|1   (force all TFT IO low/high)");
        return -1;
    }

    if (strcmp(argv[1], "init") == 0) {
        if (tft_st7789vw_init() != 0) {
            LOG_SIMPLE("tft init: initialization failed");
            return -1;
        }
        LOG_SIMPLE("tft init: display initialized");
        return 0;
    } else if (strcmp(argv[1], "pins") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("tft pins: usage: tft pins 0|1");
            return -1;
        }
        int level = atoi(argv[2]);
        GPIO_PinState state = (level == 0) ? GPIO_PIN_RESET : GPIO_PIN_SET;

        /* Control all related TFT IO: CLK, MOSI, CS, DC, RST, BL. */
        HAL_GPIO_WritePin(TFT_CLK_GPIO_Port, TFT_CLK_Pin, state);
        HAL_GPIO_WritePin(TFT_MOSI_GPIO_Port, TFT_MOSI_Pin, state);
        HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, state);
        HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, state);
        HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, state);
        HAL_GPIO_WritePin(TFT_BL_GPIO_Port, TFT_BL_Pin, state);

        LOG_SIMPLE("tft pins: set all TFT IO to %d", level);
        return 0;
    } else if (strcmp(argv[1], "fill") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("tft fill: missing color (use name or hex, e.g. red or f800)");
            return -1;
        }
        const char *color_str = argv[2];
        uint32_t color = 0U;

        /* Quick fill by name (case-insensitive): red, green, blue, white, black, yellow, cyan, magenta. */
        if (color_str[0] == 'r' || color_str[0] == 'R') {
            if (strcasecmp(color_str, "red") == 0) color = TFT_COLOR_RED;
        } else if (color_str[0] == 'g' || color_str[0] == 'G') {
            if (strcasecmp(color_str, "green") == 0) color = TFT_COLOR_GREEN;
        } else if (color_str[0] == 'b' || color_str[0] == 'B') {
            if (strcasecmp(color_str, "blue") == 0) color = TFT_COLOR_BLUE;
            else if (strcasecmp(color_str, "black") == 0) color = TFT_COLOR_BLACK;
        } else if (color_str[0] == 'w' || color_str[0] == 'W') {
            if (strcasecmp(color_str, "white") == 0) color = TFT_COLOR_WHITE;
        } else if (color_str[0] == 'y' || color_str[0] == 'Y') {
            if (strcasecmp(color_str, "yellow") == 0) color = TFT_COLOR_YELLOW;
        } else if (color_str[0] == 'c' || color_str[0] == 'C') {
            if (strcasecmp(color_str, "cyan") == 0) color = TFT_COLOR_CYAN;
        } else if (color_str[0] == 'm' || color_str[0] == 'M') {
            if (strcasecmp(color_str, "magenta") == 0) color = TFT_COLOR_MAGENTA;
        }

        if (color == 0U && color_str[0] != '\0') {
            /* Parse as hex: "0xFFFF" or "ffff"/"f800". */
            color = (uint32_t)strtoul(color_str, NULL, 0);
            if (color_str[0] != '0' || (color_str[1] != 'x' && color_str[1] != 'X')) {
                color = (uint32_t)strtoul(color_str, NULL, 16);
            }
        }

        if (tft_st7789vw_fill_screen((uint16_t)color) != 0) {
            LOG_SIMPLE("tft fill: operation failed");
            return -1;
        }
        return 0;
    } else if (strcmp(argv[1], "text") == 0) {
        if (argc < 5) {
            LOG_SIMPLE("tft text: usage: tft text <x> <y> <string>");
            return -1;
        }
        uint32_t x = (uint32_t)strtoul(argv[2], NULL, 0);
        uint32_t y = (uint32_t)strtoul(argv[3], NULL, 0);
        const char *text = argv[4];
        if (tft_st7789vw_draw_string((uint16_t)x, (uint16_t)y, text,
                                     TFT_COLOR_WHITE, TFT_COLOR_BLACK, 2U) != 0) {
            LOG_SIMPLE("tft text: operation failed");
            return -1;
        }
        return 0;
    }

    LOG_SIMPLE("Unknown tft subcommand: %s", argv[1]);
    return -1;
}

static debug_cmd_reg_t s_tft_cmd_table[] = {
    { "tft", "ST7789VW display test commands", tft_cmd },
};

static void tft_cmd_register(void)
{
    debug_cmdline_register(s_tft_cmd_table,
                           (int)(sizeof(s_tft_cmd_table) / sizeof(s_tft_cmd_table[0])));
}

void tft_st7789vw_register_commands(void)
{
    driver_cmd_register_callback("tft", tft_cmd_register);
}

