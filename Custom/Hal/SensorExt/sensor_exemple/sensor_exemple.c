/**
 * @file sensor_exemple.c
 * @brief Example: camera pipe2 -> center crop -> TFT display.
 *        Pipe2 is assumed already configured and started by the application.
 *        Command "sexp start" inits TFT, gets pipe2 params, runs preview thread.
 *        Command "sexp stop" stops thread and releases resources (does not stop camera).
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "cmsis_os2.h"
#include "debug.h"
#include "common_utils.h"
#include "dev_manager.h"
#include "camera.h"
#include "../tft_st7789v/tft_st7789vw.h"
#include "../i2c_driver/i2c_driver.h"
#include "../ltr_31x/ltr_31x.h"
#include "../sht3x/sht3x.h"
#include "../vl53l1x/vl53l1x.h"
#include "../lsm6dsr/lsm6dsr.h"
#include "../mlx90642/mlx90642_dev.h"
#include "../dts6012m/dts6012m.h"
#include "../nau881x/nau881x_cmd.h"
#include "generic_cmdline.h"
#include "drtc.h"

/* TFT size (display output) */
#define TFT_W          TFT_ST7789VW_WIDTH
#define TFT_H          TFT_ST7789VW_HEIGHT

/* RGB565 big-endian buffer for one TFT frame (one pass: RGB888->RGB565 BE, then draw_bitmap_be sends as-is) */
#define TFT_PIXELS     ((uint32_t)TFT_W * (uint32_t)TFT_H)
static uint8_t s_tft_frame_be[TFT_PIXELS * 2U] ALIGN_32;

/* Pipe2 params from application (get once at start) */
static int s_pipe2_w   = 0;
static int s_pipe2_h   = 0;
static int s_pipe2_bpp = 3;

static device_t *s_camera_dev = NULL;
static osThreadId_t s_thread_id = NULL;
static osSemaphoreId_t s_thread_done_sem = NULL;
static volatile bool s_run = false;
static volatile bool s_ir_mode = false; /* true = IR-only mode (no camera) */

/* Sensor data thread - separate thread to avoid blocking video updates */
static osThreadId_t s_sensor_thread_id = NULL;
static osMutexId_t s_sensor_data_mutex = NULL;
static osSemaphoreId_t s_sensor_thread_done_sem = NULL;
static volatile bool s_sensor_thread_run = false;

/* Shared sensor data (protected by s_sensor_data_mutex) */
typedef struct {
    float sht3x_temp;
    float sht3x_humidity;
    uint16_t als_val;
    uint16_t als_ir_val;
    lsm6dsr_data_t lsm_data;
    vl53l1x_result_t vl53_result;
    uint16_t dts6012m_dist_mm;
    bool sht3x_initialized;
    bool als_initialized;
    bool lsm6dsr_initialized;
    bool vl53l1x_initialized;
    bool dts6012m_initialized;
} sensor_data_t;

static sensor_data_t s_sensor_data = {0};

/* Sensor thread stack */
#define SENSOR_THREAD_STACK_SIZE  (2048U)
static uint8_t s_sensor_thread_stack[SENSOR_THREAD_STACK_SIZE] ALIGN_32;
static const osThreadAttr_t s_sensor_thread_attr = {
    .name = "sensor_read",
    .stack_mem = s_sensor_thread_stack,
    .stack_size = sizeof(s_sensor_thread_stack),
    .priority = osPriorityBelowNormal, /* lower than video thread to avoid FPS impact */
};

/* ---------- MLX90642 dedicated thread ---------- */
/* Pixel buffer is kept here so the display thread can render a thermal image later.
 * Unit: TO_DATA LSB = 0.02°C  →  temperature_celsius = pixel_value / 50.0f */
typedef struct {
    int16_t  pixels[MLX90642_DEV_PIXEL_COUNT]; /**< Pixel temps in TO_DATA LSB (0.02°C/LSB) */
    int16_t  min_lsb;    /**< Minimum pixel value (LSB) */
    int16_t  max_lsb;    /**< Maximum pixel value (LSB) */
    int32_t  avg_lsb;    /**< Average pixel value (LSB, sum/768) */
    int16_t  center_lsb; /**< Center pixel value (LSB) */
    bool     initialized;
} mlx_data_t;

static mlx_data_t          s_mlx_data          = {0};
static osMutexId_t         s_mlx_data_mutex     = NULL;
static osThreadId_t        s_mlx_thread_id      = NULL;
static osSemaphoreId_t     s_mlx_thread_done_sem = NULL;
static volatile bool       s_mlx_thread_run     = false;

/* Stack for the MLX thread (needs room for the 768-pixel local buffer + library) */
#define MLX_THREAD_STACK_SIZE  (4096U)
static uint8_t s_mlx_thread_stack[MLX_THREAD_STACK_SIZE] ALIGN_32;
static const osThreadAttr_t s_mlx_thread_attr = {
    .name       = "mlx_read",
    .stack_mem  = s_mlx_thread_stack,
    .stack_size = sizeof(s_mlx_thread_stack),
    .priority   = osPriorityBelowNormal, /* lower than video thread */
};

/**
 * @brief MLX90642 dedicated reading thread.
 *        Runs at below-normal priority; calls mlx90642_dev_measure_now() which
 *        blocks for the full measurement cycle (~500 ms at 2 Hz default rate).
 *        Results are published to s_mlx_data under s_mlx_data_mutex so the
 *        display thread can read stats and the full pixel array at any time.
 */
static void mlx_read_thread(void *arg)
{
    (void)arg;
    /* Static to avoid consuming 1.5 kB of the thread stack on every call frame */
    static int16_t local_pixels[MLX90642_DEV_PIXEL_COUNT];

    while (s_mlx_thread_run) {
        /* mlx90642_dev_measure_now: clears ready flag, triggers sync, waits */
        int ret = mlx90642_dev_measure_now(local_pixels);
        if (ret == 0) {
            int16_t pmin = local_pixels[0];
            int16_t pmax = local_pixels[0];
            int32_t psum = 0;
            for (uint32_t i = 0; i < MLX90642_DEV_PIXEL_COUNT; i++) {
                if (local_pixels[i] < pmin) { pmin = local_pixels[i]; }
                if (local_pixels[i] > pmax) { pmax = local_pixels[i]; }
                psum += local_pixels[i];
            }
            /* Geometric center: row 11, col 15 (0-indexed) */
            int16_t pcenter = local_pixels[11U * MLX90642_DEV_COLS + 15U];

            if (s_mlx_data_mutex != NULL) {
                osMutexAcquire(s_mlx_data_mutex, osWaitForever);
                (void)memcpy(s_mlx_data.pixels, local_pixels, sizeof(s_mlx_data.pixels));
                s_mlx_data.min_lsb     = pmin;
                s_mlx_data.max_lsb     = pmax;
                s_mlx_data.avg_lsb     = psum / (int32_t)MLX90642_DEV_PIXEL_COUNT;
                s_mlx_data.center_lsb  = pcenter;
                s_mlx_data.initialized = true;
                osMutexRelease(s_mlx_data_mutex);
            }
        }
        /* No explicit delay: measure_now already blocks for one full frame period */
    }

    if (s_mlx_thread_done_sem != NULL) {
        osSemaphoreRelease(s_mlx_thread_done_sem);
    }
    osThreadExit();
}

/**
 * @brief Sensor reading thread - reads sensors periodically without blocking video
 */
static void sensor_read_thread(void *arg)
{
    (void)arg;
    const uint32_t SENSOR_READ_INTERVAL_MS = 200; /* Read sensors every 200ms */
    
    while (s_sensor_thread_run) {
        sensor_data_t local_data;
        int ret;
        
        /* Read current shared data first (preserve old values on read failure) */
        if (s_sensor_data_mutex != NULL) {
            osMutexAcquire(s_sensor_data_mutex, osWaitForever);
            local_data = s_sensor_data;
            osMutexRelease(s_sensor_data_mutex);
        } else {
            memset(&local_data, 0, sizeof(local_data));
        }
        
        /* Read SHT3x via fetch (non-blocking: sensor measures autonomously in periodic mode) */
        ret = sht3x_fetch_data(&local_data.sht3x_temp, &local_data.sht3x_humidity);
        if (ret == 0) {
            local_data.sht3x_initialized = true;
        }
        /* Keep old values if fetch fails (sensor not present or not ready yet) */
        
        /* Read ALS - only update on success */
        ret = ltr_31x_read_als(&local_data.als_val);
        if (ret == 0) {
            local_data.als_initialized = true;
        }
        /* Read ALS IR channel - only update on success */
        ret = ltr_31x_read_als_ir(&local_data.als_ir_val);
        if (ret == 0) {
            local_data.als_initialized = true;
        }
        /* Keep old values if read fails */
        
        /* Read LSM6DSR */
        local_data.lsm6dsr_initialized = lsm6dsr_is_initialized();
        if (local_data.lsm6dsr_initialized) {
            (void)lsm6dsr_read_data(&local_data.lsm_data);
        }
        
        /* Read VL53L1X - only update on success */
        ret = vl53l1x_get_result(&local_data.vl53_result);
        if (ret == 0) {
            local_data.vl53l1x_initialized = true;
        }
        /* Keep old values if read fails */

        /* Read DTS6012M TOF - only update on success */
        {
            uint16_t dist_mm = 0;
            ret = dts6012m_get_distance_mm(&dist_mm);
            if (ret == 0) {
                local_data.dts6012m_dist_mm = dist_mm;
                local_data.dts6012m_initialized = true;
            }
        }

        /* Update shared data with mutex protection */
        if (s_sensor_data_mutex != NULL) {
            osMutexAcquire(s_sensor_data_mutex, osWaitForever);
            s_sensor_data = local_data;
            osMutexRelease(s_sensor_data_mutex);
        }
        
        osDelay(SENSOR_READ_INTERVAL_MS);
    }
    
    if (s_sensor_thread_done_sem != NULL) {
        osSemaphoreRelease(s_sensor_thread_done_sem);
    }
    osThreadExit();
}

/* One pass: crop RGB888 and write RGB565 big-endian (high byte, low byte) for direct send via draw_bitmap_be. */
static void rgb888_to_rgb565_crop_be(const uint8_t *src_rgb888,
                                     int src_width, int src_height, int src_stride,
                                     int crop_x, int crop_y,
                                     uint8_t *dst_be, int dst_width, int dst_height)
{
    const uint8_t *row = src_rgb888 + (crop_y * src_stride) + (crop_x * 3);
    for (int y = 0; y < dst_height; y++) {
        const uint8_t *p = row;
        uint8_t *out = dst_be + (y * dst_width * 2);
        for (int x = 0; x < dst_width; x++) {
            uint32_t r = p[0], g = p[1], b = p[2];
            uint16_t v = (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
            out[0] = (uint8_t)(v >> 8);
            out[1] = (uint8_t)(v & 0xFFU);
            out += 2;
            p += 3;
        }
        row += src_stride;
    }
}

/* Apply a semi-transparent black overlay (≈20% opacity) on a rectangular
 * region of an RGB565 big-endian frame buffer. This darkens the underlying
 * video slightly to improve text readability. */
static void darken_region_rgb565_be(uint8_t *buf_be,
                                    int buf_w,
                                    int buf_h,
                                    int x,
                                    int y,
                                    int w,
                                    int h)
{
    if (buf_be == NULL || buf_w <= 0 || buf_h <= 0 || w <= 0 || h <= 0) {
        return;
    }

    /* Clamp rectangle to buffer bounds */
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= buf_w || y >= buf_h || w <= 0 || h <= 0) {
        return;
    }
    if (x + w > buf_w) {
        w = buf_w - x;
    }
    if (y + h > buf_h) {
        h = buf_h - y;
    }

    /* 20% black overlay => keep 80% of original color.
     * We approximate 0.8 with (val * 4) / 5 in 5/6-bit space. */
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            uint32_t idx = ((uint32_t)yy * (uint32_t)buf_w + (uint32_t)xx) * 2U;
            uint16_t high = buf_be[idx];
            uint16_t low  = buf_be[idx + 1];
            uint16_t v = (uint16_t)((high << 8) | low);

            uint16_t r5 = (uint16_t)((v >> 11) & 0x1FU);
            uint16_t g6 = (uint16_t)((v >> 5)  & 0x3FU);
            uint16_t b5 = (uint16_t)( v        & 0x1FU);

            r5 = (uint16_t)((r5 * 4U) / 5U);
            g6 = (uint16_t)((g6 * 4U) / 5U);
            b5 = (uint16_t)((b5 * 4U) / 5U);

            v = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
            buf_be[idx]     = (uint8_t)(v >> 8);
            buf_be[idx + 1] = (uint8_t)(v & 0xFFU);
        }
    }
}

/* ---------- Iron palette (black→red→yellow→white, 256 entries) ----------
 * Classic thermal colormap: maps a normalised [0..255] intensity to RGB565 BE.
 * Segments (each 64 steps):
 *   0..63   black → red     (R rises 0→255, G=0, B=0)
 *   64..127 red   → yellow  (R=255, G rises 0→255, B=0)
 *   128..191 yellow→ white  (R=255, G=255, B rises 0→255)
 *   192..255 white (R=G=B=255)
 */
/* Pseudo-colour palette matching ir_imaging_get_pse_color():
 *   gray=  0..63  : black → blue
 *   gray= 64..127 : blue  → green  (B falls, G rises)
 *   gray=128..191 : green → yellow → red  (G=255, R rises; then R=255, G falls)
 *   gray=192..255 : yellow → red
 * gray=0 = coldest, gray=255 = hottest.
 * Returns standard RGB565 little-endian uint16_t (same convention as the TFT
 * driver's draw_pixel_to_buffer: high byte = [R4..G2], low byte = [G1..B0]). */
static uint16_t thermal_palette(float gray)
{
    uint8_t r, g, b;
    if (gray < 0.0f)   { gray = 0.0f; }
    if (gray > 255.0f) { gray = 255.0f; }

    if (gray <= 63.0f) {
        r = 0U;
        g = 0U;
        b = (uint8_t)(gray / 64.0f * 255.0f);
    } else if (gray <= 127.0f) {
        r = 0U;
        g = (uint8_t)((gray - 64.0f)  / 64.0f * 255.0f);
        b = (uint8_t)((127.0f - gray) / 64.0f * 255.0f);
    } else if (gray <= 191.0f) {
        r = (uint8_t)((gray - 128.0f) / 64.0f * 255.0f);
        g = 255U;
        b = 0U;
    } else {
        r = 255U;
        g = (uint8_t)((255.0f - gray) / 64.0f * 255.0f);
        b = 0U;
    }
    /* Pack into RGB565 LE: bits [15:11]=R, [10:5]=G, [4:0]=B */
    return (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

/* Write one RGB565 pixel into the frame buffer in big-endian byte order.
 * color is a standard RGB565 LE uint16_t (high byte first in memory = BE). */
static inline void fb_put_pixel(uint8_t *buf_be, int buf_w, int buf_h,
                                int px, int py, uint16_t color)
{
    if (px < 0 || px >= buf_w || py < 0 || py >= buf_h) { return; }
    uint32_t idx = ((uint32_t)py * (uint32_t)buf_w + (uint32_t)px) * 2U;
    buf_be[idx]     = (uint8_t)(color >> 8);    /* high byte first (BE) */
    buf_be[idx + 1] = (uint8_t)(color & 0xFFU);
}

/**
 * @brief Render the 32×24 MLX90642 pixel array as a thermal image.
 *
 * Colour mapping follows ir_imaging_get_pse_color() with a fixed temperature
 * scale snapped to 5°C boundaries (same approach as ir_imaging_draw_scale()).
 * Columns are horizontally mirrored to match the sensor's physical orientation.
 * A colour scale bar (6 px wide) with min/max temperature labels is drawn to
 * the right of the pixel grid.
 *
 * @param buf_be  RGB565 big-endian frame buffer.
 * @param buf_w   Buffer width in pixels.
 * @param buf_h   Buffer height in pixels.
 * @param pixels  768 int16_t values, unit = TO_DATA LSB (0.02°C/LSB).
 * @param dst_x   Left edge of the pixel grid in the buffer.
 * @param dst_y   Top  edge of the pixel grid in the buffer.
 * @param cell_w  Width  of each sensor pixel cell (screen pixels).
 * @param cell_h  Height of each sensor pixel cell (screen pixels).
 */
static void draw_thermal_image(uint8_t *buf_be, int buf_w, int buf_h,
                               const int16_t *pixels,
                               int dst_x, int dst_y,
                               int cell_w, int cell_h)
{
    /* Convert raw LSB values to °C (0.02°C/LSB → ÷50) and find min/max */
    float t_min = pixels[0] / 50.0f;
    float t_max = t_min;
    for (uint32_t i = 1U; i < MLX90642_DEV_PIXEL_COUNT; i++) {
        float t = pixels[i] / 50.0f;
        if (t < t_min) { t_min = t; }
        if (t > t_max) { t_max = t; }
    }

    /* Snap scale to 5°C boundaries (mirrors ir_imaging_draw_scale logic) */
    float scale_min, scale_max;
    if (t_min > 0.0f && t_min < 30.0f) {
        scale_min = 0.0f;
    } else {
        scale_min = (float)((int)(t_min / 5.0f)) * 5.0f;
    }
    if (t_max < 35.0f && t_max > 0.0f) {
        scale_max = 35.0f;
    } else {
        scale_max = (float)((int)(t_max / 5.0f) + 1) * 5.0f;
    }
    float scale_range = scale_max - scale_min;
    if (scale_range < 1.0f) { scale_range = 1.0f; }

    /* ---- draw pixel grid ---- */
    int img_w = (int)MLX90642_DEV_COLS * cell_w;
    int img_h = (int)MLX90642_DEV_ROWS * cell_h;

    for (int row = 0; row < (int)MLX90642_DEV_ROWS; row++) {
        for (int col = 0; col < (int)MLX90642_DEV_COLS; col++) {
            float t    = pixels[row * (int)MLX90642_DEV_COLS + col] / 50.0f;
            float gray = (t - scale_min) / scale_range * 255.0f;
            uint16_t color = thermal_palette(gray);

            int px0 = dst_x + col * cell_w;
            int py0 = dst_y + row * cell_h;

            for (int cy = 0; cy < cell_h; cy++) {
                for (int cx = 0; cx < cell_w; cx++) {
                    fb_put_pixel(buf_be, buf_w, buf_h, px0 + cx, py0 + cy, color);
                }
            }
        }
    }

    /* ---- colour scale bar (right of image, 6 px wide) ---- */
#define SCALE_BAR_W   6
#define SCALE_BAR_GAP 3
    int bar_x = dst_x + img_w + SCALE_BAR_GAP;
    int bar_y = dst_y;
    int bar_h = img_h;

    for (int sy = 0; sy < bar_h; sy++) {
        /* top = hottest (gray=255), bottom = coldest (gray=0) */
        float gray  = (float)(bar_h - 1 - sy) / (float)(bar_h - 1) * 255.0f;
        uint16_t color = thermal_palette(gray);
        for (int sx = 0; sx < SCALE_BAR_W; sx++) {
            fb_put_pixel(buf_be, buf_w, buf_h, bar_x + sx, bar_y + sy, color);
        }
    }

    /* ---- temperature labels beside scale bar ---- */
    {
        char tmp[12];
        int label_x = bar_x + SCALE_BAR_W + 2;

        snprintf(tmp, sizeof(tmp), "%.0fC", scale_max);
        (void)tft_st7789vw_draw_string_to_buffer(buf_be, (uint16_t)buf_w, (uint16_t)buf_h,
                                                 (uint16_t)label_x, (uint16_t)bar_y,
                                                 tmp, TFT_COLOR_WHITE, 0U, 1U);

        snprintf(tmp, sizeof(tmp), "%.0fC", scale_min);
        (void)tft_st7789vw_draw_string_to_buffer(buf_be, (uint16_t)buf_w, (uint16_t)buf_h,
                                                 (uint16_t)label_x, (uint16_t)(bar_y + bar_h - 8),
                                                 tmp, TFT_COLOR_WHITE, 0U, 1U);
    }
#undef SCALE_BAR_W
#undef SCALE_BAR_GAP
}

/* -----------------------------------------------------------------------
 * IR-only display thread.
 * Layout (240×240 TFT, all black background, no separator line):
 *   Text area : y = 4..~91  (9 lines × 10px, starting at y=4)
 *     Lines: Time, SHT3x, ALS, LSM-A, LSM-G, LSM-T, TOF, IR min/max, IR avg/ctr
 *   Thermal image: 32×24 square cells, cell=4px → 128×96px
 *   Remaining space below text: 240-91=149px, image=96px, gap=(149-96)/2=26px
 *     image top = 91+26 = 117,  image bottom = 213,  gap_bottom = 240-213 = 27px
 * ----------------------------------------------------------------------- */
#define IR_CELL_SIZE    4                                          /* square cell, px */
#define IR_CELL_W       IR_CELL_SIZE
#define IR_CELL_H       IR_CELL_SIZE
#define IR_IMG_PIX_W    ((int)MLX90642_DEV_COLS * IR_CELL_W)      /* 128 px */
#define IR_IMG_PIX_H    ((int)MLX90642_DEV_ROWS * IR_CELL_H)      /* 96  px */
#define IR_IMG_X        (((int)TFT_W - IR_IMG_PIX_W) / 2)         /* centred horizontally */
#define IR_IMG_YOFF     117                                        /* top of thermal image */

static void sensor_exemple_ir_thread(void *arg)
{
    (void)arg;
    char info_buf[256];
    const uint16_t text_fg = TFT_COLOR_WHITE;
    const uint16_t text_bg = 0;
    const uint8_t  text_scale = 1;

    /* Local copies of shared data.
     * Declared static to avoid stack overflow: mlx_data_t alone is ~1.5 kB
     * and local_pixels adds another 1.5 kB, which would exceed the thread stack. */
    static sensor_data_t sensor_data;
    static mlx_data_t    mlx_data;
    static int16_t       local_pixels[MLX90642_DEV_PIXEL_COUNT];
    bool                 have_pixels = false;
    memset(&sensor_data, 0, sizeof(sensor_data));
    memset(&mlx_data,    0, sizeof(mlx_data));

    /* Clear screen once */
    memset(s_tft_frame_be, 0, sizeof(s_tft_frame_be));
    (void)tft_st7789vw_draw_bitmap_be(0, 0, TFT_W, TFT_H, s_tft_frame_be);

    while (s_run) {
        /* ---- read shared data ---- */
        if (s_sensor_data_mutex != NULL) {
            osMutexAcquire(s_sensor_data_mutex, osWaitForever);
            sensor_data = s_sensor_data;
            osMutexRelease(s_sensor_data_mutex);
        }
        if (s_mlx_data_mutex != NULL) {
            osMutexAcquire(s_mlx_data_mutex, osWaitForever);
            mlx_data.min_lsb     = s_mlx_data.min_lsb;
            mlx_data.max_lsb     = s_mlx_data.max_lsb;
            mlx_data.avg_lsb     = s_mlx_data.avg_lsb;
            mlx_data.center_lsb  = s_mlx_data.center_lsb;
            mlx_data.initialized = s_mlx_data.initialized;
            if (s_mlx_data.initialized) {
                (void)memcpy(local_pixels, s_mlx_data.pixels, sizeof(local_pixels));
                have_pixels = true;
            }
            osMutexRelease(s_mlx_data_mutex);
        }

        /* ---- build frame buffer ---- */

        /* Full-screen black background */
        memset(s_tft_frame_be, 0, sizeof(s_tft_frame_be));

        /* Thermal image (skipped until first frame arrives; black = natural placeholder) */
        if (have_pixels) {
            draw_thermal_image(s_tft_frame_be, (int)TFT_W, (int)TFT_H,
                               local_pixels,
                               IR_IMG_X, IR_IMG_YOFF,
                               IR_CELL_W, IR_CELL_H);
        }

        /* ---- text overlay (top area) ---- */
        int y_pos = 4;

        /* Time */
        RTC_TIME_S rtc_time = rtc_get_time();
        snprintf(info_buf, sizeof(info_buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 rtc_time.year + 1960, rtc_time.month, rtc_time.date,
                 rtc_time.hour, rtc_time.minute, rtc_time.second);
        (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be, TFT_W, TFT_H,
                                                 4U, (uint16_t)y_pos,
                                                 info_buf, text_fg, text_bg, text_scale);
        y_pos += 10;

        /* SHT3x */
        if (sensor_data.sht3x_initialized) {
            snprintf(info_buf, sizeof(info_buf), "SHT3x: %.1fC %.1f%%",
                     sensor_data.sht3x_temp, sensor_data.sht3x_humidity);
        } else {
            snprintf(info_buf, sizeof(info_buf), "SHT3x: N/A");
        }
        (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be, TFT_W, TFT_H,
                                                 4U, (uint16_t)y_pos,
                                                 info_buf, text_fg, text_bg, text_scale);
        y_pos += 10;

        /* ALS */
        if (sensor_data.als_initialized) {
            snprintf(info_buf, sizeof(info_buf), "ALS: %u IR: %u",
                     sensor_data.als_val, sensor_data.als_ir_val);
        } else {
            snprintf(info_buf, sizeof(info_buf), "ALS: N/A");
        }
        (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be, TFT_W, TFT_H,
                                                 4U, (uint16_t)y_pos,
                                                 info_buf, text_fg, text_bg, text_scale);
        y_pos += 10;

        /* LSM6DSR */
        if (sensor_data.lsm6dsr_initialized) {
            snprintf(info_buf, sizeof(info_buf), "A: %.0f %.0f %.0f mg",
                     sensor_data.lsm_data.acc_x,
                     sensor_data.lsm_data.acc_y,
                     sensor_data.lsm_data.acc_z);
            (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be, TFT_W, TFT_H,
                                                     4U, (uint16_t)y_pos,
                                                     info_buf, text_fg, text_bg, text_scale);
            y_pos += 10;
            snprintf(info_buf, sizeof(info_buf), "G: %.0f %.0f %.0f mdps",
                     sensor_data.lsm_data.gyro_x,
                     sensor_data.lsm_data.gyro_y,
                     sensor_data.lsm_data.gyro_z);
            (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be, TFT_W, TFT_H,
                                                     4U, (uint16_t)y_pos,
                                                     info_buf, text_fg, text_bg, text_scale);
            y_pos += 10;
            snprintf(info_buf, sizeof(info_buf), "T: %.1f C",
                     sensor_data.lsm_data.temperature);
            (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be, TFT_W, TFT_H,
                                                     4U, (uint16_t)y_pos,
                                                     info_buf, text_fg, text_bg, text_scale);
            y_pos += 10;
        } else {
            snprintf(info_buf, sizeof(info_buf), "LSM6DSR: N/A");
            (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be, TFT_W, TFT_H,
                                                     4U, (uint16_t)y_pos,
                                                     info_buf, text_fg, text_bg, text_scale);
            y_pos += 10;
        }

        /* TOF */
        {
            char vl53_part[32], dts_part[24];
            if (sensor_data.vl53l1x_initialized) {
                if (sensor_data.vl53_result.status == VL53L1X_RANGE_STATUS_OK) {
                    snprintf(vl53_part, sizeof(vl53_part), "VL53:%umm",
                             sensor_data.vl53_result.distance_mm);
                } else {
                    snprintf(vl53_part, sizeof(vl53_part), "VL53:ERR");
                }
            } else {
                snprintf(vl53_part, sizeof(vl53_part), "VL53:N/A");
            }
            if (sensor_data.dts6012m_initialized) {
                snprintf(dts_part, sizeof(dts_part), " DTS:%umm",
                         sensor_data.dts6012m_dist_mm);
            } else {
                snprintf(dts_part, sizeof(dts_part), " DTS:N/A");
            }
            snprintf(info_buf, sizeof(info_buf), "%s%s", vl53_part, dts_part);
        }
        (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be, TFT_W, TFT_H,
                                                 4U, (uint16_t)y_pos,
                                                 info_buf, text_fg, text_bg, text_scale);
        y_pos += 10;

        /* MLX stats */
        if (mlx_data.initialized) {
            snprintf(info_buf, sizeof(info_buf),
                     "IR min:%.1f max:%.1f C",
                     mlx_data.min_lsb / 50.0f,
                     mlx_data.max_lsb / 50.0f);
            (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be, TFT_W, TFT_H,
                                                     4U, (uint16_t)y_pos,
                                                     info_buf, text_fg, text_bg, text_scale);
            y_pos += 10;
            snprintf(info_buf, sizeof(info_buf),
                     "IR avg:%.1f ctr:%.1f C",
                     mlx_data.avg_lsb    / 50.0f,
                     mlx_data.center_lsb / 50.0f);
            (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be, TFT_W, TFT_H,
                                                     4U, (uint16_t)y_pos,
                                                     info_buf, text_fg, text_bg, text_scale);
            y_pos += 10;
        } else {
            snprintf(info_buf, sizeof(info_buf), "IR(MLX90642): waiting...");
            (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be, TFT_W, TFT_H,
                                                     4U, (uint16_t)y_pos,
                                                     info_buf, text_fg, text_bg, text_scale);
            y_pos += 10;
        }


        /* ---- push to TFT ---- */
        (void)tft_st7789vw_draw_bitmap_be(0, 0, TFT_W, TFT_H, s_tft_frame_be);

        /* Pace to ~10 fps (MLX runs at 2 Hz, no point going faster) */
        osDelay(100U);
    }

    if (s_thread_done_sem != NULL) {
        osSemaphoreRelease(s_thread_done_sem);
    }
    osThreadExit();
}

static void sensor_exemple_thread(void *arg)
{
    (void)arg;
    uint8_t *fb = NULL;
    int ret;
    const int w = s_pipe2_w;
    const int h = s_pipe2_h;
    const int bpp = s_pipe2_bpp;
    const int src_stride = w * bpp;
    /* Center crop to fit TFT (min of pipe2 and TFT size) */
    const int crop_w = (w < (int)TFT_W) ? w : (int)TFT_W;
    const int crop_h = (h < (int)TFT_H) ? h : (int)TFT_H;
    const int crop_off_x = (w - crop_w) / 2;
    const int crop_off_y = (h - crop_h) / 2;

    /* FPS calculation */
    uint32_t frame_count = 0;
    uint32_t last_fps_time = osKernelGetTickCount();
    float current_fps = 0.0f;
    char info_buf[256];
    uint32_t tick_freq = osKernelGetTickFreq();

    /* Local copies of shared data.
     * Declared static to avoid stack overflow: mlx_data_t alone is ~1.5 kB. */
    static sensor_data_t sensor_data;
    static mlx_data_t    mlx_data;
    memset(&sensor_data, 0, sizeof(sensor_data));
    memset(&mlx_data,    0, sizeof(mlx_data));

    while (s_run && s_camera_dev != NULL) {
        fb = NULL;
        ret = (int)device_ioctl(s_camera_dev, CAM_CMD_GET_PIPE2_BUFFER, (uint8_t *)&fb, 0);
        if (ret <= 0 || fb == NULL) {
            osDelay(10);
            continue;
        }

        if (s_run && bpp == 3) {
            rgb888_to_rgb565_crop_be(fb, w, h, src_stride,
                                     crop_off_x, crop_off_y,
                                     s_tft_frame_be, crop_w, crop_h);

            /* Calculate FPS */
            frame_count++;
            uint32_t current_time = osKernelGetTickCount();
            uint32_t elapsed_ticks = current_time - last_fps_time;
            if (elapsed_ticks >= tick_freq) { /* Update FPS every second */
                float elapsed_seconds = (float)elapsed_ticks / tick_freq;
                current_fps = (float)frame_count / elapsed_seconds;
                frame_count = 0;
                last_fps_time = current_time;
            }

            /* Read sensor data from shared structure (fast, non-blocking) */
            if (s_sensor_data_mutex != NULL) {
                osMutexAcquire(s_sensor_data_mutex, osWaitForever);
                sensor_data = s_sensor_data;
                osMutexRelease(s_sensor_data_mutex);
            }

            /* Read MLX90642 stats (pixels skipped here; full array used for thermal image) */
            if (s_mlx_data_mutex != NULL) {
                osMutexAcquire(s_mlx_data_mutex, osWaitForever);
                mlx_data.min_lsb     = s_mlx_data.min_lsb;
                mlx_data.max_lsb     = s_mlx_data.max_lsb;
                mlx_data.avg_lsb     = s_mlx_data.avg_lsb;
                mlx_data.center_lsb  = s_mlx_data.center_lsb;
                mlx_data.initialized = s_mlx_data.initialized;
                osMutexRelease(s_mlx_data_mutex);
            }

            /* Get current time */
            RTC_TIME_S rtc_time = rtc_get_time();

            /* Draw info overlay into RGB565 BE frame buffer before sending to TFT */
            int y_pos = 5;
            const uint8_t text_scale = 1;
            const uint16_t text_fg = TFT_COLOR_WHITE;
            const uint16_t text_bg = 0; /* Unused: background pixels are skipped in buffer drawing */

            /* Darken a small top strip behind the text (~20% black) for readability */
            /* Lines: Time, FPS, SHT3x, ALS, LSM-A, LSM-G, LSM-T, TOF, IR min/max, IR avg/ctr = 10 */
            const int overlay_height = 110; /* 10 lines × 10px + margin */
            darken_region_rgb565_be(s_tft_frame_be,
                                    crop_w,
                                    crop_h,
                                    0,
                                    0,
                                    crop_w,
                                    overlay_height);

            /* Time */
            snprintf(info_buf, sizeof(info_buf), "%04d-%02d-%02d %02d:%02d:%02d",
                     rtc_time.year + 1960, rtc_time.month, rtc_time.date,
                     rtc_time.hour, rtc_time.minute, rtc_time.second);
            (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be,
                                                     (uint16_t)crop_w,
                                                     (uint16_t)crop_h,
                                                     5U,
                                                     (uint16_t)y_pos,
                                                     info_buf,
                                                     text_fg,
                                                     text_bg,
                                                     text_scale);
            y_pos += 10;

            /* FPS */
            snprintf(info_buf, sizeof(info_buf), "FPS: %.1f", current_fps);
            (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be,
                                                     (uint16_t)crop_w,
                                                     (uint16_t)crop_h,
                                                     5U,
                                                     (uint16_t)y_pos,
                                                     info_buf,
                                                     text_fg,
                                                     text_bg,
                                                     text_scale);
            y_pos += 10;

            /* SHT3x */
            if (sensor_data.sht3x_initialized) {
                snprintf(info_buf, sizeof(info_buf), "SHT3x: %.1fC %.1f%%",
                         sensor_data.sht3x_temp, sensor_data.sht3x_humidity);
            } else {
                snprintf(info_buf, sizeof(info_buf), "SHT3x: N/A");
            }
            (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be,
                                                     (uint16_t)crop_w,
                                                     (uint16_t)crop_h,
                                                     5U,
                                                     (uint16_t)y_pos,
                                                     info_buf,
                                                     text_fg,
                                                     text_bg,
                                                     text_scale);
            y_pos += 10;

            /* ALS */
            if (sensor_data.als_initialized) {
                snprintf(info_buf, sizeof(info_buf), "ALS: %u IR: %u",
                         sensor_data.als_val, sensor_data.als_ir_val);
            } else {
                snprintf(info_buf, sizeof(info_buf), "ALS: N/A");
            }
            (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be,
                                                     (uint16_t)crop_w,
                                                     (uint16_t)crop_h,
                                                     5U,
                                                     (uint16_t)y_pos,
                                                     info_buf,
                                                     text_fg,
                                                     text_bg,
                                                     text_scale);
            y_pos += 10;

            /* LSM6DSR */
            if (sensor_data.lsm6dsr_initialized) {
                snprintf(info_buf, sizeof(info_buf), "A: %.0f %.0f %.0f mg",
                         sensor_data.lsm_data.acc_x,
                         sensor_data.lsm_data.acc_y,
                         sensor_data.lsm_data.acc_z);
                (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be,
                                                         (uint16_t)crop_w,
                                                         (uint16_t)crop_h,
                                                         5U,
                                                         (uint16_t)y_pos,
                                                         info_buf,
                                                         text_fg,
                                                         text_bg,
                                                         text_scale);
                y_pos += 10;
                snprintf(info_buf, sizeof(info_buf), "G: %.0f %.0f %.0f mdps",
                         sensor_data.lsm_data.gyro_x,
                         sensor_data.lsm_data.gyro_y,
                         sensor_data.lsm_data.gyro_z);
                (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be,
                                                         (uint16_t)crop_w,
                                                         (uint16_t)crop_h,
                                                         5U,
                                                         (uint16_t)y_pos,
                                                         info_buf,
                                                         text_fg,
                                                         text_bg,
                                                         text_scale);
                y_pos += 10;
                snprintf(info_buf, sizeof(info_buf), "T: %.1f C",
                         sensor_data.lsm_data.temperature);
                (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be,
                                                         (uint16_t)crop_w,
                                                         (uint16_t)crop_h,
                                                         5U,
                                                         (uint16_t)y_pos,
                                                         info_buf,
                                                         text_fg,
                                                         text_bg,
                                                         text_scale);
                y_pos += 10;
            } else {
                snprintf(info_buf, sizeof(info_buf), "LSM6DSR: N/A");
                (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be,
                                                         (uint16_t)crop_w,
                                                         (uint16_t)crop_h,
                                                         5U,
                                                         (uint16_t)y_pos,
                                                         info_buf,
                                                         text_fg,
                                                         text_bg,
                                                         text_scale);
                y_pos += 10;
            }

            /* TOF */
            {
                char vl53_part[32], dts_part[24];
                if (sensor_data.vl53l1x_initialized) {
                    if (sensor_data.vl53_result.status == VL53L1X_RANGE_STATUS_OK) {
                        snprintf(vl53_part, sizeof(vl53_part), "VL53:%umm",
                                 sensor_data.vl53_result.distance_mm);
                    } else {
                        snprintf(vl53_part, sizeof(vl53_part), "VL53:ERR");
                    }
                } else {
                    snprintf(vl53_part, sizeof(vl53_part), "VL53:N/A");
                }
                if (sensor_data.dts6012m_initialized) {
                    snprintf(dts_part, sizeof(dts_part), " DTS:%umm",
                             sensor_data.dts6012m_dist_mm);
                } else {
                    snprintf(dts_part, sizeof(dts_part), " DTS:N/A");
                }
                snprintf(info_buf, sizeof(info_buf), "%s%s", vl53_part, dts_part);
            }
            (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be,
                                                     (uint16_t)crop_w,
                                                     (uint16_t)crop_h,
                                                     5U,
                                                     (uint16_t)y_pos,
                                                     info_buf,
                                                     text_fg,
                                                     text_bg,
                                                     text_scale);
            y_pos += 10;

            /* MLX90642: TO_DATA LSB = 0.02°C → °C = raw / 50.0f */
            if (mlx_data.initialized) {
                snprintf(info_buf, sizeof(info_buf),
                         "IR min:%.1f max:%.1f C",
                         mlx_data.min_lsb / 50.0f,
                         mlx_data.max_lsb / 50.0f);
                (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be,
                                                         (uint16_t)crop_w,
                                                         (uint16_t)crop_h,
                                                         5U,
                                                         (uint16_t)y_pos,
                                                         info_buf,
                                                         text_fg,
                                                         text_bg,
                                                         text_scale);
                y_pos += 10;
                snprintf(info_buf, sizeof(info_buf),
                         "IR avg:%.1f ctr:%.1f C",
                         mlx_data.avg_lsb    / 50.0f,
                         mlx_data.center_lsb / 50.0f);
            } else {
                snprintf(info_buf, sizeof(info_buf), "IR(MLX90642): waiting...");
            }
            (void)tft_st7789vw_draw_string_to_buffer(s_tft_frame_be,
                                                     (uint16_t)crop_w,
                                                     (uint16_t)crop_h,
                                                     5U,
                                                     (uint16_t)y_pos,
                                                     info_buf,
                                                     text_fg,
                                                     text_bg,
                                                     text_scale);

            /* Send composed frame (video + overlay text) to TFT once */
            (void)tft_st7789vw_draw_bitmap_be(0,
                                              0,
                                              (uint16_t)crop_w,
                                              (uint16_t)crop_h,
                                              s_tft_frame_be);
        }

        (void)device_ioctl(s_camera_dev, CAM_CMD_RETURN_PIPE2_BUFFER, fb, 0);
    }

    if (s_thread_done_sem != NULL) {
        osSemaphoreRelease(s_thread_done_sem);
    }
    osThreadExit();
}

static int sensor_exemple_start(bool ir_mode)
{
    pipe_params_t pipe2_param;
    int ret;

    if (s_run || s_thread_id != NULL) {
        LOG_DRV_INFO("sexp: already running");
        return -1;
    }

    s_ir_mode = ir_mode;

    if (tft_st7789vw_init() != 0) {
        LOG_DRV_ERROR("sexp: TFT init failed");
        return -1;
    }

    if (!ir_mode) {
        s_camera_dev = device_find_pattern("camera", DEV_TYPE_VIDEO);
        if (s_camera_dev == NULL) {
            LOG_DRV_ERROR("sexp: camera device not found");
            tft_st7789vw_deinit();
            return -1;
        }

        memset(&pipe2_param, 0, sizeof(pipe2_param));
        ret = (int)device_ioctl(s_camera_dev, CAM_CMD_GET_PIPE2_PARAM,
                                (uint8_t *)&pipe2_param, sizeof(pipe_params_t));
        if (ret != AICAM_OK || pipe2_param.width <= 0 || pipe2_param.height <= 0) {
            LOG_DRV_ERROR("sexp: get pipe2 param failed or invalid (ret=%d, %dx%d)",
                          ret, pipe2_param.width, pipe2_param.height);
            tft_st7789vw_deinit();
            s_camera_dev = NULL;
            return -1;
        }

        s_pipe2_w   = pipe2_param.width;
        s_pipe2_h   = pipe2_param.height;
        s_pipe2_bpp = (pipe2_param.bpp > 0) ? pipe2_param.bpp : 3;
    }

    if (s_thread_done_sem == NULL) {
        s_thread_done_sem = osSemaphoreNew(1, 0, NULL);
        if (s_thread_done_sem == NULL) {
            tft_st7789vw_deinit();
            s_camera_dev = NULL;
            return -1;
        }
    }

    /* Create mutex for sensor data sharing */
    if (s_sensor_data_mutex == NULL) {
        s_sensor_data_mutex = osMutexNew(NULL);
        if (s_sensor_data_mutex == NULL) {
            tft_st7789vw_deinit();
            s_camera_dev = NULL;
            LOG_DRV_ERROR("sexp: mutex create failed");
            return -1;
        }
    }

    /* Create semaphore for sensor thread exit notification */
    if (s_sensor_thread_done_sem == NULL) {
        s_sensor_thread_done_sem = osSemaphoreNew(1, 0, NULL);
        if (s_sensor_thread_done_sem == NULL) {
            tft_st7789vw_deinit();
            s_camera_dev = NULL;
            LOG_DRV_ERROR("sexp: sensor thread semaphore create failed");
            return -1;
        }
    }

    /* Initialize I2C port for sensors */
    ret = i2c_driver_init(I2C_PORT_1);
    if (ret != 0) {
        LOG_DRV_WARN("sexp: I2C port init failed, sensors may not work");
    }

    /* Try to initialize all sensors (ignore failures) */
    LOG_DRV_INFO("sexp: Initializing sensors...");
    
    /* Initialize SHT3x and start periodic measurement (2 Hz, high repeatability).
     * sensor_read_thread uses sht3x_fetch_data() which has no blocking delay. */
    ret = sht3x_init(SHT3X_I2C_ADDR_DEFAULT);
    if (ret == 0) {
        if (sht3x_start_periodic(SHT3X_CMD_MEASURE_PERIODIC_2_H) == 0) {
            LOG_DRV_INFO("sexp: SHT3x initialized (periodic 2Hz)");
        } else {
            LOG_DRV_INFO("sexp: SHT3x initialized (periodic start failed, single-shot fallback)");
        }
    } else {
        LOG_DRV_INFO("sexp: SHT3x init failed (may not be present)");
    }
    
    /* Initialize ALS */
    ret = ltr_31x_init();
    if (ret == 0) {
        LOG_DRV_INFO("sexp: ALS initialized");
    } else {
        LOG_DRV_INFO("sexp: ALS init failed (may not be present)");
    }
    
    /* Initialize LSM6DSR */
    ret = lsm6dsr_init(LSM6DSR_I2C_ADDR_DEFAULT);
    if (ret == 0) {
        LOG_DRV_INFO("sexp: LSM6DSR initialized");
    } else {
        LOG_DRV_INFO("sexp: LSM6DSR init failed (may not be present)");
    }
    
    /* Initialize VL53L1X */
    ret = vl53l1x_init(VL53L1X_I2C_ADDR_DEFAULT);
    if (ret == 0) {
        LOG_DRV_INFO("sexp: VL53L1X initialized");
        /* Start ranging for VL53L1X */
        (void)vl53l1x_start_ranging();
    } else {
        LOG_DRV_INFO("sexp: VL53L1X init failed (may not be present)");
    }

    /* Initialize DTS6012M TOF */
    ret = dts6012m_init(DTS6012M_I2C_ADDR_DEFAULT);
    if (ret == 0) {
        LOG_DRV_INFO("sexp: DTS6012M initialized");
    } else {
        LOG_DRV_INFO("sexp: DTS6012M init failed (may not be present)");
    }

    /* Initialize MLX90642 thermopile */
    ret = mlx90642_dev_init(MLX90642_DEV_I2C_ADDR_DEFAULT);
    if (ret == 0) {
        LOG_DRV_INFO("sexp: MLX90642 initialized");
    } else {
        LOG_DRV_INFO("sexp: MLX90642 init failed (may not be present)");
    }

    /* Create MLX data mutex */
    if (s_mlx_data_mutex == NULL) {
        s_mlx_data_mutex = osMutexNew(NULL);
        if (s_mlx_data_mutex == NULL) {
            tft_st7789vw_deinit();
            s_camera_dev = NULL;
            LOG_DRV_ERROR("sexp: MLX mutex create failed");
            return -1;
        }
    }

    /* Create MLX thread done semaphore */
    if (s_mlx_thread_done_sem == NULL) {
        s_mlx_thread_done_sem = osSemaphoreNew(1, 0, NULL);
        if (s_mlx_thread_done_sem == NULL) {
            tft_st7789vw_deinit();
            s_camera_dev = NULL;
            LOG_DRV_ERROR("sexp: MLX thread semaphore create failed");
            return -1;
        }
    }

    /* Start MLX90642 dedicated reading thread */
    s_mlx_thread_run = true;
    s_mlx_thread_id = osThreadNew(mlx_read_thread, NULL, &s_mlx_thread_attr);
    if (s_mlx_thread_id == NULL) {
        s_mlx_thread_run = false;
        tft_st7789vw_deinit();
        s_camera_dev = NULL;
        LOG_DRV_ERROR("sexp: MLX thread create failed");
        return -1;
    }

    /* Start sensor reading thread */
    s_sensor_thread_run = true;
    s_sensor_thread_id = osThreadNew(sensor_read_thread, NULL, &s_sensor_thread_attr);
    if (s_sensor_thread_id == NULL) {
        s_sensor_thread_run = false;
        tft_st7789vw_deinit();
        s_camera_dev = NULL;
        LOG_DRV_ERROR("sexp: sensor thread create failed");
        return -1;
    }

    s_run = true;
    const osThreadAttr_t attr = {
        .name = "sensor_ex",
        .stack_size = 4096U,
        .priority = osPriorityNormal,
    };
    osThreadFunc_t thread_fn = ir_mode ? sensor_exemple_ir_thread
                                       : sensor_exemple_thread;
    s_thread_id = osThreadNew(thread_fn, NULL, &attr);
    if (s_thread_id == NULL) {
        s_run = false;
        s_sensor_thread_run = false;
        tft_st7789vw_deinit();
        s_camera_dev = NULL;
        LOG_DRV_ERROR("sexp: thread create failed");
        return -1;
    }

    if (ir_mode) {
        LOG_DRV_INFO("sexp: started in IR mode (TFT %dx%d, thermal image %dx%d cells %dx%d)",
                     TFT_W, TFT_H,
                     MLX90642_DEV_COLS, MLX90642_DEV_ROWS,
                     IR_CELL_W, IR_CELL_H);
    } else {
        LOG_DRV_INFO("sexp: started (pipe2 %dx%d bpp=%d -> TFT %dx%d)",
                     s_pipe2_w, s_pipe2_h, s_pipe2_bpp, TFT_W, TFT_H);
    }
    return 0;
}

static int sensor_exemple_stop(void)
{
    uint32_t wait_ms = 3000;

    if (!s_run && s_thread_id == NULL) {
        LOG_DRV_INFO("sexp: not running");
        return 0;
    }

    /* Stop MLX90642 dedicated thread first (may be blocking in measure_now) */
    s_mlx_thread_run = false;
    if (s_mlx_thread_id != NULL && s_mlx_thread_done_sem != NULL) {
        if (osSemaphoreAcquire(s_mlx_thread_done_sem, wait_ms) != osOK) {
            LOG_DRV_WARN("sexp: MLX thread exit timeout");
        }
        /* ThreadX requires explicit terminate+delete so the static stack and TCB
         * are released from the kernel's created-thread list, allowing re-creation
         * on the next start. */
        (void)osThreadTerminate(s_mlx_thread_id);
        s_mlx_thread_id = NULL;
    }

    /* Stop sensor reading thread */
    s_sensor_thread_run = false;
    if (s_sensor_thread_id != NULL && s_sensor_thread_done_sem != NULL) {
        if (osSemaphoreAcquire(s_sensor_thread_done_sem, wait_ms) != osOK) {
            LOG_DRV_WARN("sexp: sensor thread exit timeout");
        }
        (void)osThreadTerminate(s_sensor_thread_id);
        s_sensor_thread_id = NULL;
    }

    /* Stop and deinitialize sensors */
    mlx90642_dev_deinit();
    dts6012m_deinit();
    (void)vl53l1x_stop_ranging();
    vl53l1x_deinit();
    lsm6dsr_deinit();
    ltr_31x_deinit();
    (void)sht3x_stop_periodic();
    sht3x_deinit();
    i2c_driver_deinit(I2C_PORT_1);

    s_run = false;

    if (s_thread_id != NULL && s_thread_done_sem != NULL) {
        if (osSemaphoreAcquire(s_thread_done_sem, wait_ms) != osOK) {
            LOG_DRV_WARN("sexp: thread exit timeout");
        }
        (void)osThreadTerminate(s_thread_id);
        s_thread_id = NULL;
    }

    s_camera_dev = NULL;
    tft_st7789vw_deinit();
    LOG_DRV_INFO("sexp: stopped, resources released (camera/pipe2 unchanged)");
    return 0;
}

static int sensor_exemple_cmd(int argc, char **argv)
{
    if (argc < 2) {
        LOG_SIMPLE("Usage:");
        LOG_SIMPLE("  sexp start      - init TFT, get pipe2 params, run camera preview");
        LOG_SIMPLE("  sexp start ir   - init TFT, run IR thermal image (no camera needed)");
        LOG_SIMPLE("  sexp stop       - stop preview and release resources");
        return -1;
    }

    if (strcmp(argv[1], "start") == 0) {
        bool ir_mode = (argc >= 3 && strcmp(argv[2], "ir") == 0);
        return sensor_exemple_start(ir_mode);
    }
    if (strcmp(argv[1], "stop") == 0) {
        return sensor_exemple_stop();
    }

    LOG_SIMPLE("sexp: unknown subcommand '%s'", argv[1]);
    return -1;
}

static debug_cmd_reg_t s_sensor_exemple_cmd_table[] = {
    { "sexp", "Camera pipe2 -> TFT preview (start/stop)", sensor_exemple_cmd },
};

static void sensor_exemple_cmd_register(void)
{
    debug_cmdline_register(s_sensor_exemple_cmd_table,
                          (int)(sizeof(s_sensor_exemple_cmd_table) / sizeof(s_sensor_exemple_cmd_table[0])));
}

/* --- LTR-311ALS-02 ALS sensor test command --- */
static int als_cmd(int argc, char **argv)
{
    int ret;
    uint16_t als_val, ir_val;
    uint8_t id;

    if (argc < 2) {
        LOG_SIMPLE("Usage: als <subcmd> [args]");
        LOG_SIMPLE("  als init         - init LTR-310/LTR-311 sensor (I2C port 1, addr 0x22)");
        LOG_SIMPLE("  als read         - read ALS and IR counts");
        LOG_SIMPLE("  als deinit       - deinit and release I2C");
        LOG_SIMPLE("  als reset        - software reset IC");
        LOG_SIMPLE("  als id           - read PART_ID and MANUFAC_ID");
        LOG_SIMPLE("  als status       - read ALS_STATUS");
        LOG_SIMPLE("  als thresh <hi> <lo> - set thresholds (hex or decimal)");
        LOG_SIMPLE("  als timing <scale> <int_steps> <mrr_steps> - set timing regs (hex or decimal)");
        LOG_SIMPLE("  als avg <val>    - set ALS averaging (0x7F)");
        LOG_SIMPLE("  als ir_enable <0|1> - set IR channel enable (0x95)");
        LOG_SIMPLE("  als ir_enable_r - read IR enable status");
        LOG_SIMPLE("  als int_cfg <polarity> <mode> - set interrupt config (0xA0): polarity(0=low/1=high), mode(0=inactive/1=enabled)");
        LOG_SIMPLE("  als int_cfg_r  - read interrupt config");
        LOG_SIMPLE("  als int_persist <count> - set interrupt persist count (0xA1, 0-15)");
        LOG_SIMPLE("  als int_persist_r - read interrupt persist count");
        return -1;
    }

    if (strcmp(argv[1], "init") == 0) {
        ret = i2c_driver_init(I2C_PORT_1);
        if (ret != 0) {
            LOG_SIMPLE("als: i2c_driver_init failed %d", ret);
            return -1;
        }
        ret = ltr_31x_init();
        if (ret == 0) {
            LOG_SIMPLE("als: init OK");
            return 0;
        }
        if (ret == AICAM_ERROR_ALREADY_INITIALIZED) {
            LOG_SIMPLE("als: already inited");
            return 0;
        }
        LOG_SIMPLE("als: init failed %d", ret);
        i2c_driver_deinit(I2C_PORT_1);
        return -1;
    }

    if (strcmp(argv[1], "read") == 0) {
        ret = ltr_31x_read_als(&als_val);
        if (ret != 0) {
            LOG_SIMPLE("als: read_als failed %d", ret);
            return -1;
        }
        ret = ltr_31x_read_als_ir(&ir_val);
        if (ret != 0) {
            LOG_SIMPLE("als: read_als_ir failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("als: ALS=%u IR=%u", (unsigned)als_val, (unsigned)ir_val);
        return 0;
    }

    if (strcmp(argv[1], "deinit") == 0) {
        ltr_31x_deinit();
        i2c_driver_deinit(I2C_PORT_1);
        LOG_SIMPLE("als: deinit done (sensor + I2C port)");
        return 0;
    }

    if (strcmp(argv[1], "reset") == 0) {
        ret = ltr_31x_reset();
        if (ret == 0) {
            LOG_SIMPLE("als: reset OK, ALS re-enabled (no re-init needed)");
        } else {
            LOG_SIMPLE("als: reset failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "id") == 0) {
        ret = ltr_31x_get_part_id(&id);
        if (ret != 0) {
            LOG_SIMPLE("als: get_part_id failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("als: PART_ID=0x%02X", id);
        ret = ltr_31x_get_manufac_id(&id);
        if (ret != 0) {
            LOG_SIMPLE("als: get_manufac_id failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("als: MANUFAC_ID=0x%02X", id);
        return 0;
    }

    if (strcmp(argv[1], "status") == 0) {
        ret = ltr_31x_read_status(&id);
        if (ret != 0) {
            LOG_SIMPLE("als: read_status failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("als: ALS_STATUS=0x%02X", id);
        return 0;
    }

    if (strcmp(argv[1], "thresh") == 0) {
        if (argc < 4) {
            LOG_SIMPLE("als thresh <high> <low> (hex or decimal)");
            return -1;
        }
        unsigned int hi = (unsigned int)strtoul(argv[2], NULL, 0);
        unsigned int lo = (unsigned int)strtoul(argv[3], NULL, 0);
        if (hi > 0xFFFFU || lo > 0xFFFFU) {
            LOG_SIMPLE("als: threshold out of range");
            return -1;
        }
        ret = ltr_31x_set_thresholds((uint16_t)hi, (uint16_t)lo);
        if (ret == 0) {
            LOG_SIMPLE("als: thresholds set high=0x%04X low=0x%04X", hi, lo);
        } else {
            LOG_SIMPLE("als: set_thresholds failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "timing") == 0) {
        if (argc < 5) {
            LOG_SIMPLE("als timing <time_scale> <int_steps> <mrr_steps> (hex or decimal)");
            return -1;
        }
        unsigned int scale = (unsigned int)strtoul(argv[2], NULL, 0);
        unsigned int isteps = (unsigned int)strtoul(argv[3], NULL, 0);
        unsigned int mrr = (unsigned int)strtoul(argv[4], NULL, 0);
        if (scale > 0xFFU || isteps > 0xFFU || mrr > 0xFFU) {
            LOG_SIMPLE("als: timing values out of range");
            return -1;
        }
        ret = ltr_31x_set_timing((uint8_t)scale, (uint8_t)isteps, (uint8_t)mrr);
        if (ret == 0) {
            LOG_SIMPLE("als: timing set scale=0x%02X int_steps=0x%02X mrr_steps=0x%02X", scale, isteps, mrr);
        } else {
            LOG_SIMPLE("als: set_timing failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "avg") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("als avg <value> (hex or decimal, register 0x7F)");
            return -1;
        }
        unsigned int v = (unsigned int)strtoul(argv[2], NULL, 0);
        if (v > 0xFFU) {
            LOG_SIMPLE("als: avg value out of range");
            return -1;
        }
        ret = ltr_31x_set_als_averaging((uint8_t)v);
        if (ret == 0) {
            LOG_SIMPLE("als: averaging set 0x%02X", (unsigned)v);
        } else {
            LOG_SIMPLE("als: set_als_averaging failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "ir_enable") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("als ir_enable <0|1> (0=disabled, 1=enabled)");
            return -1;
        }
        unsigned int v = (unsigned int)strtoul(argv[2], NULL, 0);
        if (v > 1U) {
            LOG_SIMPLE("als: ir_enable must be 0 or 1");
            return -1;
        }
        ret = ltr_31x_set_ir_enable(v != 0U);
        if (ret == 0) {
            LOG_SIMPLE("als: IR enable set %s", v ? "enabled" : "disabled");
        } else {
            LOG_SIMPLE("als: set_ir_enable failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "ir_enable_r") == 0) {
        bool enabled;
        ret = ltr_31x_get_ir_enable(&enabled);
        if (ret == 0) {
            LOG_SIMPLE("als: IR enable = %s", enabled ? "enabled" : "disabled");
        } else {
            LOG_SIMPLE("als: get_ir_enable failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "int_cfg") == 0) {
        if (argc < 4) {
            LOG_SIMPLE("als int_cfg <polarity> <mode> (polarity: 0=low/1=high, mode: 0=inactive/1=enabled)");
            return -1;
        }
        unsigned int pol = (unsigned int)strtoul(argv[2], NULL, 0);
        unsigned int mode = (unsigned int)strtoul(argv[3], NULL, 0);
        if (pol > 1U || mode > 1U) {
            LOG_SIMPLE("als: polarity and mode must be 0 or 1");
            return -1;
        }
        ret = ltr_31x_set_interrupt_config(pol != 0U, mode != 0U);
        if (ret == 0) {
            LOG_SIMPLE("als: interrupt config set polarity=%s mode=%s",
                        pol ? "high" : "low", mode ? "enabled" : "inactive");
        } else {
            LOG_SIMPLE("als: set_interrupt_config failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "int_cfg_r") == 0) {
        bool polarity, mode;
        ret = ltr_31x_get_interrupt_config(&polarity, &mode);
        if (ret == 0) {
            LOG_SIMPLE("als: interrupt config polarity=%s mode=%s",
                        polarity ? "high" : "low", mode ? "enabled" : "inactive");
        } else {
            LOG_SIMPLE("als: get_interrupt_config failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "int_persist") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("als int_persist <count> (0-15, consecutive out-of-range measurements before interrupt)");
            return -1;
        }
        unsigned int v = (unsigned int)strtoul(argv[2], NULL, 0);
        if (v > 15U) {
            LOG_SIMPLE("als: persist_count must be 0-15");
            return -1;
        }
        ret = ltr_31x_set_interrupt_persist((uint8_t)v);
        if (ret == 0) {
            LOG_SIMPLE("als: interrupt persist set %u", (unsigned)v);
        } else {
            LOG_SIMPLE("als: set_interrupt_persist failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "int_persist_r") == 0) {
        uint8_t persist;
        ret = ltr_31x_get_interrupt_persist(&persist);
        if (ret == 0) {
            LOG_SIMPLE("als: interrupt persist = %u", (unsigned)persist);
        } else {
            LOG_SIMPLE("als: get_interrupt_persist failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    LOG_SIMPLE("als: unknown subcommand '%s'", argv[1]);
    return -1;
}

static debug_cmd_reg_t s_als_cmd_table[] = {
    { "als", "LTR-310/LTR-311 ALS sensor test (init/read/deinit/reset/id/status/thresh/timing/avg/ir_enable/int_cfg/int_persist)", als_cmd },
};

static void als_cmd_register(void)
{
    debug_cmdline_register(s_als_cmd_table,
                            (int)(sizeof(s_als_cmd_table) / sizeof(s_als_cmd_table[0])));
}

/* --- SHT3x Temperature and Humidity sensor test command --- */
static int sht3x_cmd(int argc, char **argv)
{
    int ret;
    float temperature, humidity;
    uint16_t status;
    uint8_t i2c_addr = SHT3X_I2C_ADDR_DEFAULT;

    if (argc < 2) {
        LOG_SIMPLE("Usage: sht3x <subcmd> [args]");
        LOG_SIMPLE("  sht3x init [addr]     - init SHT3x sensor (I2C port 1, default addr 0x44, optional 0x45)");
        LOG_SIMPLE("  sht3x read            - read temperature and humidity (single shot, high repeatability)");
        LOG_SIMPLE("  sht3x read_h          - read with high repeatability");
        LOG_SIMPLE("  sht3x read_m          - read with medium repeatability");
        LOG_SIMPLE("  sht3x read_l          - read with low repeatability");
        LOG_SIMPLE("  sht3x periodic_start  - start periodic measurement (1 mps, high repeatability)");
        LOG_SIMPLE("  sht3x periodic_fetch  - fetch data from periodic measurement");
        LOG_SIMPLE("  sht3x periodic_stop   - stop periodic measurement");
        LOG_SIMPLE("  sht3x reset           - software reset sensor");
        LOG_SIMPLE("  sht3x heater <0|1>    - enable/disable heater (0=off, 1=on)");
        LOG_SIMPLE("  sht3x status          - read status register");
        LOG_SIMPLE("  sht3x clear_status    - clear status register");
        LOG_SIMPLE("  sht3x deinit          - deinit and release I2C");
        return -1;
    }

    if (strcmp(argv[1], "init") == 0) {
        if (argc >= 3) {
            unsigned int addr = (unsigned int)strtoul(argv[2], NULL, 0);
            if (addr != SHT3X_I2C_ADDR_DEFAULT && addr != SHT3X_I2C_ADDR_ALT) {
                LOG_SIMPLE("sht3x: invalid I2C address (use 0x44 or 0x45)");
                return -1;
            }
            i2c_addr = (uint8_t)addr;
        }
        ret = i2c_driver_init(I2C_PORT_1);
        if (ret != 0) {
            LOG_SIMPLE("sht3x: i2c_driver_init failed %d", ret);
            return -1;
        }
        ret = sht3x_init(i2c_addr);
        if (ret == 0) {
            LOG_SIMPLE("sht3x: init OK (addr=0x%02X)", i2c_addr);
            return 0;
        }
        if (ret == AICAM_ERROR_ALREADY_INITIALIZED) {
            LOG_SIMPLE("sht3x: already inited");
            return 0;
        }
        LOG_SIMPLE("sht3x: init failed %d", ret);
        i2c_driver_deinit(I2C_PORT_1);
        return -1;
    }

    if (strcmp(argv[1], "read") == 0 || strcmp(argv[1], "read_h") == 0) {
        ret = sht3x_read_measurement(&temperature, &humidity);
        if (ret != 0) {
            LOG_SIMPLE("sht3x: read_measurement failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("sht3x: Temperature=%.2f°C Humidity=%.2f%%RH", temperature, humidity);
        return 0;
    }

    if (strcmp(argv[1], "read_m") == 0) {
        ret = sht3x_read_measurement_cmd(SHT3X_CMD_MEASURE_SINGLE_M, &temperature, &humidity);
        if (ret != 0) {
            LOG_SIMPLE("sht3x: read_measurement (medium) failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("sht3x: Temperature=%.2f°C Humidity=%.2f%%RH (medium repeatability)", temperature, humidity);
        return 0;
    }

    if (strcmp(argv[1], "read_l") == 0) {
        ret = sht3x_read_measurement_cmd(SHT3X_CMD_MEASURE_SINGLE_L, &temperature, &humidity);
        if (ret != 0) {
            LOG_SIMPLE("sht3x: read_measurement (low) failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("sht3x: Temperature=%.2f°C Humidity=%.2f%%RH (low repeatability)", temperature, humidity);
        return 0;
    }

    if (strcmp(argv[1], "periodic_start") == 0) {
        ret = sht3x_start_periodic(SHT3X_CMD_MEASURE_PERIODIC_1_H);
        if (ret == 0) {
            LOG_SIMPLE("sht3x: periodic measurement started (1 mps, high repeatability)");
        } else {
            LOG_SIMPLE("sht3x: start_periodic failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "periodic_fetch") == 0) {
        ret = sht3x_fetch_data(&temperature, &humidity);
        if (ret != 0) {
            LOG_SIMPLE("sht3x: fetch_data failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("sht3x: Temperature=%.2f°C Humidity=%.2f%%RH (periodic)", temperature, humidity);
        return 0;
    }

    if (strcmp(argv[1], "periodic_stop") == 0) {
        ret = sht3x_stop_periodic();
        if (ret == 0) {
            LOG_SIMPLE("sht3x: periodic measurement stopped");
        } else {
            LOG_SIMPLE("sht3x: stop_periodic failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "reset") == 0) {
        ret = sht3x_reset();
        if (ret == 0) {
            LOG_SIMPLE("sht3x: reset OK");
        } else {
            LOG_SIMPLE("sht3x: reset failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "heater") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("sht3x heater <0|1> (0=off, 1=on)");
            return -1;
        }
        unsigned int v = (unsigned int)strtoul(argv[2], NULL, 0);
        if (v > 1U) {
            LOG_SIMPLE("sht3x: heater must be 0 or 1");
            return -1;
        }
        ret = sht3x_set_heater(v != 0U);
        if (ret == 0) {
            LOG_SIMPLE("sht3x: heater %s", v ? "enabled" : "disabled");
        } else {
            LOG_SIMPLE("sht3x: set_heater failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "status") == 0) {
        ret = sht3x_read_status(&status);
        if (ret != 0) {
            LOG_SIMPLE("sht3x: read_status failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("sht3x: Status=0x%04X", status);
        LOG_SIMPLE("  Alert pending: %s", (status & 0x8000) ? "yes" : "no");
        LOG_SIMPLE("  Heater status: %s", (status & 0x2000) ? "on" : "off");
        LOG_SIMPLE("  RH tracking alert: %s", (status & 0x0800) ? "yes" : "no");
        LOG_SIMPLE("  T tracking alert: %s", (status & 0x0400) ? "yes" : "no");
        LOG_SIMPLE("  System reset detected: %s", (status & 0x0010) ? "yes" : "no");
        LOG_SIMPLE("  Command status: %s", (status & 0x0002) ? "error" : "ok");
        LOG_SIMPLE("  Write data checksum status: %s", (status & 0x0001) ? "error" : "ok");
        return 0;
    }

    if (strcmp(argv[1], "clear_status") == 0) {
        ret = sht3x_clear_status();
        if (ret == 0) {
            LOG_SIMPLE("sht3x: status cleared");
        } else {
            LOG_SIMPLE("sht3x: clear_status failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "deinit") == 0) {
        sht3x_deinit();
        i2c_driver_deinit(I2C_PORT_1);
        LOG_SIMPLE("sht3x: deinit done (sensor + I2C port)");
        return 0;
    }

    LOG_SIMPLE("sht3x: unknown subcommand '%s'", argv[1]);
    return -1;
}

static debug_cmd_reg_t s_sht3x_cmd_table[] = {
    { "sht3x", "SHT3x temperature/humidity sensor test (init/read/periodic/reset/heater/status/deinit)", sht3x_cmd },
};

static void sht3x_cmd_register(void)
{
    debug_cmdline_register(s_sht3x_cmd_table,
                            (int)(sizeof(s_sht3x_cmd_table) / sizeof(s_sht3x_cmd_table[0])));
}

/* --- VL53L1X ToF ranging sensor test command --- */
static int vl53l1x_cmd(int argc, char **argv)
{
    int ret;
    uint16_t distance_mm;
    vl53l1x_result_t result;
    bool is_ready;
    uint16_t sensor_id;
    uint8_t major, minor, build;
    uint32_t revision;
    vl53l1x_distance_mode_t mode;
    uint16_t timing_budget;
    uint16_t period_ms;
    bool polarity;
    int16_t offset;
    uint16_t roi_width, roi_height;

    if (argc < 2) {
        LOG_SIMPLE("Usage: vl53l1x <subcmd> [args]");
        LOG_SIMPLE("  vl53l1x init [addr]     - init VL53L1X sensor (I2C port 1, default addr 0x52)");
        LOG_SIMPLE("  vl53l1x start           - start ranging operation");
        LOG_SIMPLE("  vl53l1x stop            - stop ranging operation");
        LOG_SIMPLE("  vl53l1x read            - read distance measurement (polling mode)");
        LOG_SIMPLE("  vl53l1x result          - get complete ranging result");
        LOG_SIMPLE("  vl53l1x check           - check if data is ready");
        LOG_SIMPLE("  vl53l1x clear           - clear interrupt");
        LOG_SIMPLE("  vl53l1x mode <1|2>      - set distance mode (1=short, 2=long)");
        LOG_SIMPLE("  vl53l1x mode_r          - read distance mode");
        LOG_SIMPLE("  vl53l1x timing <ms>     - set timing budget (15/20/33/50/100/200/500 ms)");
        LOG_SIMPLE("  vl53l1x timing_r        - read timing budget");
        LOG_SIMPLE("  vl53l1x period <ms>     - set inter-measurement period (must >= timing budget)");
        LOG_SIMPLE("  vl53l1x period_r        - read inter-measurement period");
        LOG_SIMPLE("  vl53l1x polarity <0|1>  - set interrupt polarity (0=low, 1=high)");
        LOG_SIMPLE("  vl53l1x polarity_r      - read interrupt polarity");
        LOG_SIMPLE("  vl53l1x offset <mm>     - set offset correction in mm");
        LOG_SIMPLE("  vl53l1x offset_r        - read offset correction");
        LOG_SIMPLE("  vl53l1x roi <w> <h>     - set ROI size (minimum 4x4)");
        LOG_SIMPLE("  vl53l1x roi_r           - read ROI size");
        LOG_SIMPLE("  vl53l1x id              - read sensor ID (should be 0xEEAC)");
        LOG_SIMPLE("  vl53l1x version         - read software version");
        LOG_SIMPLE("  vl53l1x deinit          - deinit and release I2C");
        return -1;
    }

    if (strcmp(argv[1], "init") == 0) {
        uint8_t i2c_addr = VL53L1X_I2C_ADDR_DEFAULT;
        if (argc >= 3) {
            unsigned int addr = (unsigned int)strtoul(argv[2], NULL, 0);
            if (addr == 0 || addr > 0x7F) {
                LOG_SIMPLE("vl53l1x: invalid I2C address");
                return -1;
            }
            i2c_addr = (uint8_t)addr;
        }
        ret = i2c_driver_init(I2C_PORT_1);
        if (ret != 0) {
            LOG_SIMPLE("vl53l1x: i2c_driver_init failed %d", ret);
            return -1;
        }
        ret = vl53l1x_init(i2c_addr);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: init OK");
            return 0;
        }
        if (ret == AICAM_ERROR_ALREADY_INITIALIZED) {
            LOG_SIMPLE("vl53l1x: already inited");
            return 0;
        }
        LOG_SIMPLE("vl53l1x: init failed %d", ret);
        i2c_driver_deinit(I2C_PORT_1);
        return -1;
    }

    if (strcmp(argv[1], "start") == 0) {
        ret = vl53l1x_start_ranging();
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: ranging started");
        } else {
            LOG_SIMPLE("vl53l1x: start_ranging failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "stop") == 0) {
        ret = vl53l1x_stop_ranging();
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: ranging stopped");
        } else {
            LOG_SIMPLE("vl53l1x: stop_ranging failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "read") == 0) {
        ret = vl53l1x_get_distance(&distance_mm);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: Distance=%u mm", (unsigned)distance_mm);
        } else {
            LOG_SIMPLE("vl53l1x: get_distance failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "result") == 0) {
        ret = vl53l1x_get_result(&result);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: Status=%u Distance=%u mm Signal=%u kcps Ambient=%u kcps SPADs=%u",
                        (unsigned)result.status, (unsigned)result.distance_mm,
                        (unsigned)result.signal_rate, (unsigned)result.ambient_rate,
                        (unsigned)result.spad_count);
        } else {
            LOG_SIMPLE("vl53l1x: get_result failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "check") == 0) {
        ret = vl53l1x_check_data_ready(&is_ready);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: Data ready = %s", is_ready ? "yes" : "no");
        } else {
            LOG_SIMPLE("vl53l1x: check_data_ready failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "clear") == 0) {
        ret = vl53l1x_clear_interrupt();
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: interrupt cleared");
        } else {
            LOG_SIMPLE("vl53l1x: clear_interrupt failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "mode") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("vl53l1x mode <1|2> (1=short, 2=long)");
            return -1;
        }
        unsigned int m = (unsigned int)strtoul(argv[2], NULL, 0);
        if (m != 1 && m != 2) {
            LOG_SIMPLE("vl53l1x: mode must be 1 or 2");
            return -1;
        }
        ret = vl53l1x_set_distance_mode((m == 1) ? VL53L1X_DISTANCE_MODE_SHORT : VL53L1X_DISTANCE_MODE_LONG);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: distance mode set to %s", m == 1 ? "short" : "long");
        } else {
            LOG_SIMPLE("vl53l1x: set_distance_mode failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "mode_r") == 0) {
        ret = vl53l1x_get_distance_mode(&mode);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: distance mode = %s",
                        mode == VL53L1X_DISTANCE_MODE_SHORT ? "short" : "long");
        } else {
            LOG_SIMPLE("vl53l1x: get_distance_mode failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "timing") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("vl53l1x timing <ms> (15/20/33/50/100/200/500)");
            return -1;
        }
        unsigned int t = (unsigned int)strtoul(argv[2], NULL, 0);
        ret = vl53l1x_set_timing_budget((uint16_t)t);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: timing budget set to %u ms", t);
        } else {
            LOG_SIMPLE("vl53l1x: set_timing_budget failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "timing_r") == 0) {
        ret = vl53l1x_get_timing_budget(&timing_budget);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: timing budget = %u ms", (unsigned)timing_budget);
        } else {
            LOG_SIMPLE("vl53l1x: get_timing_budget failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "period") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("vl53l1x period <ms> (must be >= timing budget)");
            return -1;
        }
        unsigned int p = (unsigned int)strtoul(argv[2], NULL, 0);
        ret = vl53l1x_set_intermeasurement_period((uint32_t)p);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: inter-measurement period set to %u ms", p);
        } else {
            LOG_SIMPLE("vl53l1x: set_intermeasurement_period failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "period_r") == 0) {
        ret = vl53l1x_get_intermeasurement_period(&period_ms);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: inter-measurement period = %u ms", (unsigned)period_ms);
        } else {
            LOG_SIMPLE("vl53l1x: get_intermeasurement_period failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "polarity") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("vl53l1x polarity <0|1> (0=low, 1=high)");
            return -1;
        }
        unsigned int p = (unsigned int)strtoul(argv[2], NULL, 0);
        if (p > 1) {
            LOG_SIMPLE("vl53l1x: polarity must be 0 or 1");
            return -1;
        }
        ret = vl53l1x_set_interrupt_polarity(p != 0);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: interrupt polarity set to %s", p ? "high" : "low");
        } else {
            LOG_SIMPLE("vl53l1x: set_interrupt_polarity failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "polarity_r") == 0) {
        ret = vl53l1x_get_interrupt_polarity(&polarity);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: interrupt polarity = %s", polarity ? "high" : "low");
        } else {
            LOG_SIMPLE("vl53l1x: get_interrupt_polarity failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "offset") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("vl53l1x offset <mm> (offset correction in mm)");
            return -1;
        }
        int o = (int)strtol(argv[2], NULL, 0);
        ret = vl53l1x_set_offset((int16_t)o);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: offset set to %d mm", o);
        } else {
            LOG_SIMPLE("vl53l1x: set_offset failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "offset_r") == 0) {
        ret = vl53l1x_get_offset(&offset);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: offset = %d mm", offset);
        } else {
            LOG_SIMPLE("vl53l1x: get_offset failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "roi") == 0) {
        if (argc < 4) {
            LOG_SIMPLE("vl53l1x roi <width> <height> (minimum 4x4)");
            return -1;
        }
        unsigned int w = (unsigned int)strtoul(argv[2], NULL, 0);
        unsigned int h = (unsigned int)strtoul(argv[3], NULL, 0);
        ret = vl53l1x_set_roi((uint16_t)w, (uint16_t)h);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: ROI set to %ux%u", w, h);
        } else {
            LOG_SIMPLE("vl53l1x: set_roi failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "roi_r") == 0) {
        ret = vl53l1x_get_roi(&roi_width, &roi_height);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: ROI = %ux%u", (unsigned)roi_width, (unsigned)roi_height);
        } else {
            LOG_SIMPLE("vl53l1x: get_roi failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "id") == 0) {
        ret = vl53l1x_get_sensor_id(&sensor_id);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: Sensor ID = 0x%04X (expected 0xEEAC)", sensor_id);
        } else {
            LOG_SIMPLE("vl53l1x: get_sensor_id failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "version") == 0) {
        ret = vl53l1x_get_sw_version(&major, &minor, &build, &revision);
        if (ret == 0) {
            LOG_SIMPLE("vl53l1x: SW version = %u.%u.%u (revision %lu)",
                        (unsigned)major, (unsigned)minor, (unsigned)build, (unsigned long)revision);
        } else {
            LOG_SIMPLE("vl53l1x: get_sw_version failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "deinit") == 0) {
        vl53l1x_deinit();
        i2c_driver_deinit(I2C_PORT_1);
        LOG_SIMPLE("vl53l1x: deinit done (sensor + I2C port)");
        return 0;
    }

    LOG_SIMPLE("vl53l1x: unknown subcommand '%s'", argv[1]);
    return -1;
}

static debug_cmd_reg_t s_vl53l1x_cmd_table[] = {
    { "vl53l1x", "VL53L1X ToF ranging sensor test (init/start/stop/read/result/mode/timing/period/polarity/offset/roi/id/version/deinit)", vl53l1x_cmd },
};

static void vl53l1x_cmd_register(void)
{
    debug_cmdline_register(s_vl53l1x_cmd_table,
                            (int)(sizeof(s_vl53l1x_cmd_table) / sizeof(s_vl53l1x_cmd_table[0])));
}

/* --- LSM6DSR IMU sensor test command --- */
static int lsm6dsr_cmd(int argc, char **argv)
{
    int ret;
    lsm6dsr_data_t data;
    bool ready;
    uint8_t device_id;
    uint8_t i2c_addr = LSM6DSR_I2C_ADDR_DEFAULT;
    unsigned int odr_val, fs_val;

    if (argc < 2) {
        LOG_SIMPLE("Usage: lsm6dsr <subcmd> [args]");
        LOG_SIMPLE("  lsm6dsr init [addr]     - init LSM6DSR sensor (I2C port 1, default addr 0x6A, optional 0x6B)");
        LOG_SIMPLE("  lsm6dsr read            - read accelerometer, gyroscope and temperature data");
        LOG_SIMPLE("  lsm6dsr acc_config <odr> <fs> - config accelerometer (odr: 0=off,1=12.5Hz,2=26Hz,3=52Hz,4=104Hz,5=208Hz,6=416Hz,7=833Hz,8=1666Hz,9=3332Hz,10=6664Hz; fs: 0=±2g,1=±4g,2=±8g,3=±16g)");
        LOG_SIMPLE("  lsm6dsr gyro_config <odr> <fs> - config gyroscope (odr: same as acc; fs: 0=±125dps,1=±250dps,2=±500dps,3=±1000dps,4=±2000dps)");
        LOG_SIMPLE("  lsm6dsr acc_ready       - check if accelerometer data is ready");
        LOG_SIMPLE("  lsm6dsr gyro_ready      - check if gyroscope data is ready");
        LOG_SIMPLE("  lsm6dsr temp_ready      - check if temperature data is ready");
        LOG_SIMPLE("  lsm6dsr id              - read device ID");
        LOG_SIMPLE("  lsm6dsr reset           - reset sensor");
        LOG_SIMPLE("  lsm6dsr deinit          - deinit and release I2C");
        return -1;
    }

    if (strcmp(argv[1], "init") == 0) {
        if (argc >= 3) {
            unsigned int addr = (unsigned int)strtoul(argv[2], NULL, 0);
            if (addr != LSM6DSR_I2C_ADDR_LOW && addr != LSM6DSR_I2C_ADDR_HIGH) {
                LOG_SIMPLE("lsm6dsr: invalid I2C address (use 0x6A or 0x6B)");
                return -1;
            }
            i2c_addr = (uint8_t)addr;
        }
        ret = i2c_driver_init(I2C_PORT_1);
        if (ret != 0) {
            LOG_SIMPLE("lsm6dsr: i2c_driver_init failed %d", ret);
            return -1;
        }
        ret = lsm6dsr_init(i2c_addr);
        if (ret == 0) {
            LOG_SIMPLE("lsm6dsr: init OK (addr=0x%02X)", i2c_addr);
            return 0;
        }
        if (ret == AICAM_ERROR_ALREADY_INITIALIZED) {
            LOG_SIMPLE("lsm6dsr: already inited");
            return 0;
        }
        LOG_SIMPLE("lsm6dsr: init failed %d", ret);
        i2c_driver_deinit(I2C_PORT_1);
        return -1;
    }

    if (strcmp(argv[1], "read") == 0) {
        ret = lsm6dsr_read_data(&data);
        if (ret != 0) {
            LOG_SIMPLE("lsm6dsr: read_data failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("lsm6dsr: Acc X=%.2f mg, Y=%.2f mg, Z=%.2f mg", 
                     data.acc_x, data.acc_y, data.acc_z);
        LOG_SIMPLE("lsm6dsr: Gyro X=%.2f mdps, Y=%.2f mdps, Z=%.2f mdps",
                     data.gyro_x, data.gyro_y, data.gyro_z);
        LOG_SIMPLE("lsm6dsr: Temperature=%.2f°C", data.temperature);
        return 0;
    }

    if (strcmp(argv[1], "acc_config") == 0) {
        if (argc < 4) {
            LOG_SIMPLE("lsm6dsr: acc_config requires ODR and FS arguments");
            return -1;
        }
        odr_val = (unsigned int)strtoul(argv[2], NULL, 0);
        fs_val = (unsigned int)strtoul(argv[3], NULL, 0);
        if (odr_val > 10 || fs_val > 3) {
            LOG_SIMPLE("lsm6dsr: invalid ODR or FS value");
            return -1;
        }
        ret = lsm6dsr_acc_config((lsm6dsr_odr_t)odr_val, (lsm6dsr_acc_fs_t)fs_val);
        if (ret == 0) {
            LOG_SIMPLE("lsm6dsr: accelerometer configured (ODR=%u, FS=%u)", odr_val, fs_val);
        } else {
            LOG_SIMPLE("lsm6dsr: acc_config failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "gyro_config") == 0) {
        if (argc < 4) {
            LOG_SIMPLE("lsm6dsr: gyro_config requires ODR and FS arguments");
            return -1;
        }
        odr_val = (unsigned int)strtoul(argv[2], NULL, 0);
        fs_val = (unsigned int)strtoul(argv[3], NULL, 0);
        if (odr_val > 10 || fs_val > 4) {
            LOG_SIMPLE("lsm6dsr: invalid ODR or FS value");
            return -1;
        }
        ret = lsm6dsr_gyro_config((lsm6dsr_odr_t)odr_val, (lsm6dsr_gyro_fs_t)fs_val);
        if (ret == 0) {
            LOG_SIMPLE("lsm6dsr: gyroscope configured (ODR=%u, FS=%u)", odr_val, fs_val);
        } else {
            LOG_SIMPLE("lsm6dsr: gyro_config failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "acc_ready") == 0) {
        ret = lsm6dsr_acc_data_ready(&ready);
        if (ret != 0) {
            LOG_SIMPLE("lsm6dsr: acc_data_ready failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("lsm6dsr: accelerometer data ready: %s", ready ? "yes" : "no");
        return 0;
    }

    if (strcmp(argv[1], "gyro_ready") == 0) {
        ret = lsm6dsr_gyro_data_ready(&ready);
        if (ret != 0) {
            LOG_SIMPLE("lsm6dsr: gyro_data_ready failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("lsm6dsr: gyroscope data ready: %s", ready ? "yes" : "no");
        return 0;
    }

    if (strcmp(argv[1], "temp_ready") == 0) {
        ret = lsm6dsr_temp_data_ready(&ready);
        if (ret != 0) {
            LOG_SIMPLE("lsm6dsr: temp_data_ready failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("lsm6dsr: temperature data ready: %s", ready ? "yes" : "no");
        return 0;
    }

    if (strcmp(argv[1], "id") == 0) {
        ret = lsm6dsr_get_device_id(&device_id);
        if (ret != 0) {
            LOG_SIMPLE("lsm6dsr: get_device_id failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("lsm6dsr: Device ID = 0x%02X (expected 0x%02X)", 
                     device_id, LSM6DSR_DEVICE_ID);
        return 0;
    }

    if (strcmp(argv[1], "reset") == 0) {
        ret = lsm6dsr_reset();
        if (ret == 0) {
            LOG_SIMPLE("lsm6dsr: reset OK");
        } else {
            LOG_SIMPLE("lsm6dsr: reset failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "deinit") == 0) {
        lsm6dsr_deinit();
        i2c_driver_deinit(I2C_PORT_1);
        LOG_SIMPLE("lsm6dsr: deinit OK");
        return 0;
    }

    LOG_SIMPLE("lsm6dsr: unknown subcommand '%s'", argv[1]);
    return -1;
}

static debug_cmd_reg_t s_lsm6dsr_cmd_table[] = {
    { "lsm6dsr", "LSM6DSR IMU sensor test (init/read/acc_config/gyro_config/acc_ready/gyro_ready/temp_ready/id/reset/deinit)", lsm6dsr_cmd },
};

static void lsm6dsr_cmd_register(void)
{
    debug_cmdline_register(s_lsm6dsr_cmd_table,
                            (int)(sizeof(s_lsm6dsr_cmd_table) / sizeof(s_lsm6dsr_cmd_table[0])));
}

/* --- MLX90642 IR thermopile array sensor test command --- */

/**
 * Print a pixel temperature value.
 * MLX90642 TO_DATA LSB = 0.02°C (i.e. pixel_value / 50 = degrees Celsius).
 * We display with 2 decimal places: whole = val/50, frac = (val%50)*2 (in 0.01°C units).
 */
static void print_pixel_celsius(const char *prefix, int16_t val)
{
    /* Compute sign separately so negative values display correctly */
    int sign  = (val < 0) ? -1 : 1;
    int aval  = (val < 0) ? -val : val;
    int whole = (int)(aval / 50);
    /* remainder in units of 0.02°C → convert to centi-°C (*2) for 2-digit fraction */
    int frac  = (int)((aval % 50) * 2);
    if (sign < 0) {
        LOG_SIMPLE("%s-%d.%02d C", prefix, whole, frac);
    } else {
        LOG_SIMPLE("%s%d.%02d C", prefix, whole, frac);
    }
}

static int mlx90642_cmd(int argc, char **argv)
{
    int ret;

    if (argc < 2) {
        LOG_SIMPLE("Usage: mlx90642 <subcmd> [args]");
        LOG_SIMPLE("  mlx90642 init [addr]      - init sensor (I2C1, default addr 0x66)");
        LOG_SIMPLE("  mlx90642 measure          - one sync measurement, print min/max/center pixel");
        LOG_SIMPLE("  mlx90642 dump             - measure and dump all 768 pixel temps (centi-C)");
        LOG_SIMPLE("  mlx90642 id               - read 64-bit unique device ID");
        LOG_SIMPLE("  mlx90642 version          - read firmware version");
        LOG_SIMPLE("  mlx90642 rate <2|4|8|16|32> - set refresh rate in Hz");
        LOG_SIMPLE("  mlx90642 rate_r           - read current refresh rate");
        LOG_SIMPLE("  mlx90642 emissivity <val> - set emissivity (0x0001..0x4000, 0x4000=1.0, 0x3D71~0.95)");
        LOG_SIMPLE("  mlx90642 emissivity_r     - read current emissivity");
        LOG_SIMPLE("  mlx90642 treflected <val> - set reflected temp (centi-C, e.g. 2500=25.00C)");
        LOG_SIMPLE("  mlx90642 treflected_r     - read current reflected temp");
        LOG_SIMPLE("  mlx90642 mode <cont|step> - set measurement mode");
        LOG_SIMPLE("  mlx90642 mode_r           - read measurement mode");
        LOG_SIMPLE("  mlx90642 format <temp|norm> - set output format (temperature or normalized)");
        LOG_SIMPLE("  mlx90642 format_r         - read output format");
        LOG_SIMPLE("  mlx90642 status           - read data-ready / busy / read-window flags");
        LOG_SIMPLE("  mlx90642 sleep            - send sensor to sleep");
        LOG_SIMPLE("  mlx90642 wakeup           - wake sensor from sleep");
        LOG_SIMPLE("  mlx90642 deinit           - deinit sensor and release I2C instance");
        return -1;
    }

    if (strcmp(argv[1], "init") == 0) {
        uint8_t addr = MLX90642_DEV_I2C_ADDR_DEFAULT;
        if (argc >= 3) {
            unsigned int a = (unsigned int)strtoul(argv[2], NULL, 0);
            if (a == 0 || a > 0x7FU) {
                LOG_SIMPLE("mlx90642: invalid I2C address");
                return -1;
            }
            addr = (uint8_t)a;
        }
        ret = i2c_driver_init(I2C_PORT_1);
        if (ret != 0) {
            LOG_SIMPLE("mlx90642: i2c_driver_init failed %d", ret);
            return -1;
        }
        ret = mlx90642_dev_init(addr);
        if (ret == 0) {
            LOG_SIMPLE("mlx90642: init OK (addr=0x%02X)", addr);
            return 0;
        }
        if (ret == AICAM_ERROR_ALREADY_INITIALIZED) {
            LOG_SIMPLE("mlx90642: already inited");
            return 0;
        }
        LOG_SIMPLE("mlx90642: init failed %d", ret);
        i2c_driver_deinit(I2C_PORT_1);
        return -1;
    }

    if (strcmp(argv[1], "measure") == 0) {
        static int16_t s_pixels[MLX90642_DEV_PIXEL_COUNT];
        ret = mlx90642_dev_measure_now(s_pixels);
        if (ret != 0) {
            LOG_SIMPLE("mlx90642: measure_now failed %d", ret);
            return -1;
        }
        int16_t pmin = s_pixels[0], pmax = s_pixels[0];
        int32_t psum = 0;
        for (int i = 0; i < MLX90642_DEV_PIXEL_COUNT; i++) {
            if (s_pixels[i] < pmin) { pmin = s_pixels[i]; }
            if (s_pixels[i] > pmax) { pmax = s_pixels[i]; }
            psum += s_pixels[i];
        }
        int16_t pcenter = s_pixels[(MLX90642_DEV_ROWS / 2) * MLX90642_DEV_COLS +
                                    MLX90642_DEV_COLS / 2];
        int16_t pavg = (int16_t)(psum / MLX90642_DEV_PIXEL_COUNT);
        print_pixel_celsius("mlx90642: min    = ", pmin);
        print_pixel_celsius("mlx90642: max    = ", pmax);
        print_pixel_celsius("mlx90642: avg    = ", pavg);
        print_pixel_celsius("mlx90642: center = ", pcenter);
        return 0;
    }

    if (strcmp(argv[1], "dump") == 0) {
        static int16_t s_pixels[MLX90642_DEV_PIXEL_COUNT];
        ret = mlx90642_dev_measure_now(s_pixels);
        if (ret != 0) {
            LOG_SIMPLE("mlx90642: measure_now failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("mlx90642: pixel dump (row x col, unit=0.02C, raw/50=degC):");
        for (int r = 0; r < (int)MLX90642_DEV_ROWS; r++) {
            char row_buf[MLX90642_DEV_COLS * 7 + 1];
            int  pos = 0;
            for (int c = 0; c < (int)MLX90642_DEV_COLS; c++) {
                int16_t v = s_pixels[r * MLX90642_DEV_COLS + c];
                pos += snprintf(row_buf + pos, sizeof(row_buf) - (size_t)pos, "%6d", (int)v);
            }
            LOG_SIMPLE("%s", row_buf);
        }
        return 0;
    }

    if (strcmp(argv[1], "id") == 0) {
        uint16_t id4[4];
        ret = mlx90642_dev_get_id(id4);
        if (ret != 0) {
            LOG_SIMPLE("mlx90642: get_id failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("mlx90642: ID = %04X %04X %04X %04X",
                     (unsigned)id4[0], (unsigned)id4[1],
                     (unsigned)id4[2], (unsigned)id4[3]);
        return 0;
    }

    if (strcmp(argv[1], "version") == 0) {
        mlx90642_dev_fw_ver_t ver;
        ret = mlx90642_dev_get_fw_version(&ver);
        if (ret != 0) {
            LOG_SIMPLE("mlx90642: get_fw_version failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("mlx90642: FW version = %u.%u.%u",
                     (unsigned)ver.major, (unsigned)ver.minor, (unsigned)ver.patch);
        return 0;
    }

    if (strcmp(argv[1], "rate") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("mlx90642 rate <2|4|8|16|32>");
            return -1;
        }
        unsigned int hz = (unsigned int)strtoul(argv[2], NULL, 0);
        mlx90642_dev_rate_t rate;
        switch (hz) {
            case 2:  rate = MLX90642_DEV_RATE_2HZ;  break;
            case 4:  rate = MLX90642_DEV_RATE_4HZ;  break;
            case 8:  rate = MLX90642_DEV_RATE_8HZ;  break;
            case 16: rate = MLX90642_DEV_RATE_16HZ; break;
            case 32: rate = MLX90642_DEV_RATE_32HZ; break;
            default:
                LOG_SIMPLE("mlx90642: unsupported rate %u (use 2/4/8/16/32)", hz);
                return -1;
        }
        ret = mlx90642_dev_set_refresh_rate(rate);
        if (ret == 0) {
            LOG_SIMPLE("mlx90642: refresh rate set to %u Hz", hz);
        } else {
            LOG_SIMPLE("mlx90642: set_refresh_rate failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "rate_r") == 0) {
        mlx90642_dev_rate_t rate;
        ret = mlx90642_dev_get_refresh_rate(&rate);
        if (ret != 0) {
            LOG_SIMPLE("mlx90642: get_refresh_rate failed %d", ret);
            return -1;
        }
        static const unsigned int rate_hz_table[] = {0, 0, 2, 4, 8, 16, 32};
        unsigned int hz = ((unsigned int)rate < sizeof(rate_hz_table) / sizeof(rate_hz_table[0]))
                          ? rate_hz_table[(unsigned int)rate] : 0U;
        LOG_SIMPLE("mlx90642: refresh rate = %u Hz (reg val %d)", hz, (int)rate);
        return 0;
    }

    if (strcmp(argv[1], "emissivity") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("mlx90642 emissivity <val> (1..0x4000, 0x4000=1.0)");
            return -1;
        }
        unsigned int v = (unsigned int)strtoul(argv[2], NULL, 0);
        if (v == 0 || v > 0x4000U) {
            LOG_SIMPLE("mlx90642: emissivity must be 1..0x4000");
            return -1;
        }
        ret = mlx90642_dev_set_emissivity((int16_t)v);
        if (ret == 0) {
            /* emissivity fraction: val/0x4000, display as 0.XXXX using integer math */
            unsigned int pct10000 = (unsigned int)(((uint32_t)v * 10000U) / 0x4000U);
            LOG_SIMPLE("mlx90642: emissivity set to 0x%04X (%u.%04u)",
                         v, pct10000 / 10000U, pct10000 % 10000U);
        } else {
            LOG_SIMPLE("mlx90642: set_emissivity failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "emissivity_r") == 0) {
        int16_t e;
        ret = mlx90642_dev_get_emissivity(&e);
        if (ret != 0) {
            LOG_SIMPLE("mlx90642: get_emissivity failed %d", ret);
            return -1;
        }
        unsigned int ev = (unsigned int)(uint16_t)e;
        unsigned int pct10000 = (unsigned int)(((uint32_t)ev * 10000U) / 0x4000U);
        LOG_SIMPLE("mlx90642: emissivity = 0x%04X (%u.%04u)",
                     ev, pct10000 / 10000U, pct10000 % 10000U);
        return 0;
    }

    if (strcmp(argv[1], "treflected") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("mlx90642 treflected <centi-C> (e.g. 2500 = 25.00 C)");
            return -1;
        }
        int v = (int)strtol(argv[2], NULL, 0);
        ret = mlx90642_dev_set_treflected((int16_t)v);
        if (ret == 0) {
            LOG_SIMPLE("mlx90642: Treflected set to %d centi-C (%d.%02d C)",
                         v, v / 100, (v < 0 ? -v : v) % 100);
        } else {
            LOG_SIMPLE("mlx90642: set_treflected failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "treflected_r") == 0) {
        int16_t tr = 0;
        ret = mlx90642_dev_get_treflected(&tr);
        if (ret != 0) {
            LOG_SIMPLE("mlx90642: read treflected failed %d", ret);
            return -1;
        }
        if ((uint16_t)tr == 0x8000U) {
            LOG_SIMPLE("mlx90642: Treflected = default (Tsensor-9, 0x8000)");
        } else {
            LOG_SIMPLE("mlx90642: Treflected = %d centi-C (%d.%02d C)",
                         (int)tr, (int)(tr / 100), (int)((tr < 0 ? -tr : tr) % 100));
        }
        return 0;
    }

    if (strcmp(argv[1], "mode") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("mlx90642 mode <cont|step>");
            return -1;
        }
        mlx90642_dev_meas_mode_t mode;
        if (strcmp(argv[2], "cont") == 0) {
            mode = MLX90642_DEV_MODE_CONTINUOUS;
        } else if (strcmp(argv[2], "step") == 0) {
            mode = MLX90642_DEV_MODE_STEP;
        } else {
            LOG_SIMPLE("mlx90642: mode must be 'cont' or 'step'");
            return -1;
        }
        ret = mlx90642_dev_set_meas_mode(mode);
        if (ret == 0) {
            LOG_SIMPLE("mlx90642: measurement mode set to %s", argv[2]);
        } else {
            LOG_SIMPLE("mlx90642: set_meas_mode failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "mode_r") == 0) {
        mlx90642_dev_meas_mode_t mode;
        ret = mlx90642_dev_get_meas_mode(&mode);
        if (ret != 0) {
            LOG_SIMPLE("mlx90642: get_meas_mode failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("mlx90642: measurement mode = %s",
                     mode == MLX90642_DEV_MODE_CONTINUOUS ? "continuous" : "step");
        return 0;
    }

    if (strcmp(argv[1], "format") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("mlx90642 format <temp|norm>");
            return -1;
        }
        mlx90642_dev_output_fmt_t fmt;
        if (strcmp(argv[2], "temp") == 0) {
            fmt = MLX90642_DEV_OUTPUT_TEMPERATURE;
        } else if (strcmp(argv[2], "norm") == 0) {
            fmt = MLX90642_DEV_OUTPUT_NORMALIZED;
        } else {
            LOG_SIMPLE("mlx90642: format must be 'temp' or 'norm'");
            return -1;
        }
        ret = mlx90642_dev_set_output_format(fmt);
        if (ret == 0) {
            LOG_SIMPLE("mlx90642: output format set to %s", argv[2]);
        } else {
            LOG_SIMPLE("mlx90642: set_output_format failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "format_r") == 0) {
        mlx90642_dev_output_fmt_t fmt;
        ret = mlx90642_dev_get_output_format(&fmt);
        if (ret != 0) {
            LOG_SIMPLE("mlx90642: get_output_format failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("mlx90642: output format = %s",
                     fmt == MLX90642_DEV_OUTPUT_TEMPERATURE ? "temperature" : "normalized");
        return 0;
    }

    if (strcmp(argv[1], "status") == 0) {
        bool ready, busy, window_open;
        ret = mlx90642_dev_is_data_ready(&ready);
        if (ret != 0) {
            LOG_SIMPLE("mlx90642: is_data_ready failed %d", ret);
            return -1;
        }
        ret = mlx90642_dev_is_busy(&busy);
        if (ret != 0) {
            LOG_SIMPLE("mlx90642: is_busy failed %d", ret);
            return -1;
        }
        ret = mlx90642_dev_is_read_window_open(&window_open);
        if (ret != 0) {
            LOG_SIMPLE("mlx90642: is_read_window_open failed %d", ret);
            return -1;
        }
        LOG_SIMPLE("mlx90642: data_ready=%s  busy=%s  read_window=%s",
                     ready ? "yes" : "no",
                     busy  ? "yes" : "no",
                     window_open ? "open" : "closed");
        return 0;
    }

    if (strcmp(argv[1], "sleep") == 0) {
        ret = mlx90642_dev_sleep();
        if (ret == 0) {
            LOG_SIMPLE("mlx90642: sensor sleeping");
        } else {
            LOG_SIMPLE("mlx90642: sleep failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "wakeup") == 0) {
        ret = mlx90642_dev_wakeup();
        if (ret == 0) {
            LOG_SIMPLE("mlx90642: sensor awake");
        } else {
            LOG_SIMPLE("mlx90642: wakeup failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "deinit") == 0) {
        mlx90642_dev_deinit();
        i2c_driver_deinit(I2C_PORT_1);
        LOG_SIMPLE("mlx90642: deinit done (sensor + I2C port)");
        return 0;
    }

    LOG_SIMPLE("mlx90642: unknown subcommand '%s'", argv[1]);
    return -1;
}

static debug_cmd_reg_t s_mlx90642_cmd_table[] = {
    { "mlx90642", "MLX90642 IR 32x24 thermopile array test (init/measure/dump/id/version/rate/emissivity/treflected/mode/format/status/sleep/wakeup/deinit)", mlx90642_cmd },
};

static void mlx90642_cmd_register(void)
{
    debug_cmdline_register(s_mlx90642_cmd_table,
                           (int)(sizeof(s_mlx90642_cmd_table) / sizeof(s_mlx90642_cmd_table[0])));
}

/* --- DTS6012M TOF ranging sensor test command --- */
static int dts6012m_cmd(int argc, char **argv)
{
    int ret;

    if (argc < 2) {
        LOG_SIMPLE("Usage: dts6012m <subcmd> [args]");
        LOG_SIMPLE("  dts6012m init [addr]  - init sensor (I2C1, default addr 0x51), laser on");
        LOG_SIMPLE("  dts6012m read         - read distance in mm and m");
        LOG_SIMPLE("  dts6012m laser <0|1>  - turn laser off (0) or on (1)");
        LOG_SIMPLE("  dts6012m deinit       - turn laser off, release I2C instance");
        return -1;
    }

    if (strcmp(argv[1], "init") == 0) {
        uint8_t addr = DTS6012M_I2C_ADDR_DEFAULT;
        if (argc >= 3) {
            unsigned int a = (unsigned int)strtoul(argv[2], NULL, 0);
            if (a == 0 || a > 0x7FU) {
                LOG_SIMPLE("dts6012m: invalid I2C address");
                return -1;
            }
            addr = (uint8_t)a;
        }
        ret = i2c_driver_init(I2C_PORT_1);
        if (ret != 0) {
            LOG_SIMPLE("dts6012m: i2c_driver_init failed %d", ret);
            return -1;
        }
        ret = dts6012m_init(addr);
        if (ret == 0) {
            LOG_SIMPLE("dts6012m: init OK (addr=0x%02X, laser on)", addr);
            return 0;
        }
        if (ret == AICAM_ERROR_ALREADY_INITIALIZED) {
            LOG_SIMPLE("dts6012m: already inited");
            return 0;
        }
        LOG_SIMPLE("dts6012m: init failed %d", ret);
        i2c_driver_deinit(I2C_PORT_1);
        return -1;
    }

    if (strcmp(argv[1], "read") == 0) {
        uint16_t mm;
        ret = dts6012m_get_distance_mm(&mm);
        if (ret != 0) {
            LOG_SIMPLE("dts6012m: read failed %d", ret);
            return -1;
        }
        /* Print as "X.XXX m" without float formatting overhead */
        uint32_t m_whole = mm / 1000U;
        uint32_t m_frac  = mm % 1000U;
        LOG_SIMPLE("dts6012m: %u mm  (%lu.%03lu m)",
                     (unsigned)mm, (unsigned long)m_whole, (unsigned long)m_frac);
        return 0;
    }

    if (strcmp(argv[1], "laser") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("dts6012m laser <0|1>");
            return -1;
        }
        unsigned int v = (unsigned int)strtoul(argv[2], NULL, 0);
        if (v > 1U) {
            LOG_SIMPLE("dts6012m: laser must be 0 or 1");
            return -1;
        }
        ret = (v != 0U) ? dts6012m_start_laser() : dts6012m_stop_laser();
        if (ret == 0) {
            LOG_SIMPLE("dts6012m: laser %s", v ? "on" : "off");
        } else {
            LOG_SIMPLE("dts6012m: laser control failed %d", ret);
        }
        return (ret == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "deinit") == 0) {
        dts6012m_deinit();
        i2c_driver_deinit(I2C_PORT_1);
        LOG_SIMPLE("dts6012m: deinit done (laser off, sensor + I2C port released)");
        return 0;
    }

    LOG_SIMPLE("dts6012m: unknown subcommand '%s'", argv[1]);
    return -1;
}

static debug_cmd_reg_t s_dts6012m_cmd_table[] = {
    { "dts6012m", "DTS6012M TOF LIDAR sensor test (init/read/laser/deinit)", dts6012m_cmd },
};

static void dts6012m_cmd_register(void)
{
    debug_cmdline_register(s_dts6012m_cmd_table,
                           (int)(sizeof(s_dts6012m_cmd_table) / sizeof(s_dts6012m_cmd_table[0])));
}

void sensor_exemple_register_commands(void)
{
    driver_cmd_register_callback("sexp", sensor_exemple_cmd_register);
    driver_cmd_register_callback("als", als_cmd_register);
    driver_cmd_register_callback("sht3x", sht3x_cmd_register);
    driver_cmd_register_callback("vl53l1x", vl53l1x_cmd_register);
    driver_cmd_register_callback("lsm6dsr", lsm6dsr_cmd_register);
    driver_cmd_register_callback("mlx90642", mlx90642_cmd_register);
    driver_cmd_register_callback("dts6012m", dts6012m_cmd_register);
    driver_cmd_register_callback("nau881x", nau881x_cmd_register);
}
