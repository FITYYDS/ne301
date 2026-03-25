/**
 * @file tft_st7789vw.h
 * @brief ST7789VW SPI display driver (SPI6, RGB565).
 *
 * This driver provides a small, LVGL‑friendly API for region drawing,
 * basic primitives (pixel/line/rect) and simple English text drawing.
 *
 * Hardware assumptions:
 * - Connected to STM32N6 SPI6 (MOSI/SCK/NSS already configured in CubeMX).
 * - Reset pin:  `TFT_RST_Pin` / `TFT_RST_GPIO_Port`.
 * - Backlight:  `TFT_BL_Pin`  / `TFT_BL_GPIO_Port` (optional but recommended).
 *
 * NOTE:
 * - Command/Data (D/C) handling is hardware‑specific for ST7789 3‑wire/4‑wire
 *   modes. This driver assumes a separate D/C GPIO. If your board uses
 *   3‑wire 9‑bit SPI, you should re‑implement the low‑level write helpers in
 *   `tft_st7789vw.c` to match your hardware.
 */

#ifndef TFT_ST7789VW_H
#define TFT_ST7789VW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Display resolution (logical) */
#define TFT_ST7789VW_WIDTH        240U
#define TFT_ST7789VW_HEIGHT       240U

/* Color definitions in RGB565 */
#define TFT_COLOR_BLACK           0x0000U
#define TFT_COLOR_WHITE           0xFFFFU
#define TFT_COLOR_RED             0xF800U
#define TFT_COLOR_GREEN           0x07E0U
#define TFT_COLOR_BLUE            0x001FU
#define TFT_COLOR_CYAN            0x07FFU
#define TFT_COLOR_MAGENTA         0xF81FU
#define TFT_COLOR_YELLOW          0xFFE0U

typedef enum {
    TFT_ORIENTATION_PORTRAIT_0   = 0,
    TFT_ORIENTATION_LANDSCAPE_90 = 1,
    TFT_ORIENTATION_PORTRAIT_180 = 2,
    TFT_ORIENTATION_LANDSCAPE_270= 3,
} tft_orientation_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    tft_orientation_t orientation;
    bool initialized;
} tft_st7789vw_handle_t;

/**
 * @brief Initialize ST7789VW display on SPI6.
 * @return 0 on success, negative value on error.
 */
int tft_st7789vw_init(void);

/**
 * @brief Deinitialize display (put into sleep mode and turn off backlight).
 */
void tft_st7789vw_deinit(void);

/**
 * @brief Get internal handle (read‑only for application).
 */
const tft_st7789vw_handle_t *tft_st7789vw_get_handle(void);

/**
 * @brief Set display orientation.
 */
int tft_st7789vw_set_orientation(tft_orientation_t orientation);

/**
 * @brief Fill entire screen with a single color.
 */
int tft_st7789vw_fill_screen(uint16_t color);

/**
 * @brief Fill rectangular region with a color.
 *
 * Region is clipped to display bounds.
 */
int tft_st7789vw_fill_rect(uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h,
                           uint16_t color);

/**
 * @brief Draw rectangle border (unfilled).
 */
int tft_st7789vw_draw_rect(uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h,
                           uint16_t color);

/**
 * @brief Draw a single pixel.
 */
int tft_st7789vw_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief Draw a line using Bresenham algorithm.
 */
int tft_st7789vw_draw_line(int16_t x0, int16_t y0,
                           int16_t x1, int16_t y1,
                           uint16_t color);

/**
 * @brief Draw a bitmap region (RGB565, host byte order).
 *
 * Converts to big-endian when sending. Use for normal uint16_t RGB565 buffers
 * (e.g. LVGL). For already big-endian data use tft_st7789vw_draw_bitmap_be().
 */
int tft_st7789vw_draw_bitmap(uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h,
                             const uint16_t *pixels);

/**
 * @brief Draw a bitmap from buffer already in display byte order (big-endian).
 *
 * `pixels_be` must be w*h*2 bytes, row-major, each pixel high byte then low byte.
 * No internal swap; single-pass send. Use when caller can produce BE (e.g. RGB888->RGB565 in one pass).
 */
int tft_st7789vw_draw_bitmap_be(uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h,
                                const uint8_t *pixels_be);

/**
 * @brief Draw a single 5x7 ASCII character (scaled).
 *
 * Origin is top‑left corner of character rectangle.
 */
int tft_st7789vw_draw_char(uint16_t x, uint16_t y, char c,
                           uint16_t fg_color, uint16_t bg_color,
                           uint8_t scale);

/**
 * @brief Draw a zero‑terminated ASCII string.
 */
int tft_st7789vw_draw_string(uint16_t x, uint16_t y, const char *str,
                             uint16_t fg_color, uint16_t bg_color,
                             uint8_t scale);

/**
 * @brief Draw a single 5x7 ASCII character (scaled) into an RGB565 BE buffer.
 *
 * The buffer format matches tft_st7789vw_draw_bitmap_be(): row-major,
 * each pixel high byte then low byte (big-endian RGB565).
 */
int tft_st7789vw_draw_char_to_buffer(uint8_t *buf_be,
                                     uint16_t buf_w,
                                     uint16_t buf_h,
                                     uint16_t x,
                                     uint16_t y,
                                     char c,
                                     uint16_t fg_color,
                                     uint16_t bg_color,
                                     uint8_t scale);

/**
 * @brief Draw a zero-terminated ASCII string into an RGB565 BE buffer.
 *
 * Convenience wrapper over tft_st7789vw_draw_char_to_buffer(). Uses the same
 * 5x7 font and layout as tft_st7789vw_draw_string(), but writes into a
 * caller-provided buffer instead of the SPI display.
 */
int tft_st7789vw_draw_string_to_buffer(uint8_t *buf_be,
                                       uint16_t buf_w,
                                       uint16_t buf_h,
                                       uint16_t x,
                                       uint16_t y,
                                       const char *str,
                                       uint16_t fg_color,
                                       uint16_t bg_color,
                                       uint8_t scale);

/**
 * @brief Register debug commands for this driver.
 *
 * Commands (examples, see implementation for details):
 *   - tft fill <color>
 *   - tft text <x> <y> <string>
 */
void tft_st7789vw_register_commands(void);

#ifdef __cplusplus
}
#endif

#endif /* TFT_ST7789VW_H */

