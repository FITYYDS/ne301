/**
 ******************************************************************************
 * @file    image_utils.c
 * @author  GPM Application Team
 * @brief   Image processing utilities implementation for AI model cascade
 *
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#include "image_utils.h"
#include "draw.h"
#include "dev_manager.h"
#include "pixel_format_map.h"
#include "mem.h"
#include "debug.h"
#include "stm32n6xx_hal.h"
#include "stm32n6xx_hal_dma2d.h"
#include "cmsis_os2.h"
#include <string.h>
#include <math.h>
#include <stdint.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

/**
 * @brief Bilinear interpolation for a single pixel
 */
static void bilinear_interpolate_pixel(
    const uint8_t *src,
    uint32_t src_width,
    uint32_t src_height,
    uint32_t src_bytes_per_pixel,
    float x,
    float y,
    uint8_t *dst,
    uint32_t dst_bytes_per_pixel
)
{
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = MIN(x0 + 1, (int)src_width - 1);
    int y1 = MIN(y0 + 1, (int)src_height - 1);
    
    float fx = x - x0;
    float fy = y - y0;
    float fx1 = 1.0f - fx;
    float fy1 = 1.0f - fy;
    
    // Clamp coordinates
    x0 = MAX(0, MIN(x0, (int)src_width - 1));
    y0 = MAX(0, MIN(y0, (int)src_height - 1));
    
    // Get four corner pixels
    const uint8_t *p00 = src + (y0 * src_width + x0) * src_bytes_per_pixel;
    const uint8_t *p01 = src + (y0 * src_width + x1) * src_bytes_per_pixel;
    const uint8_t *p10 = src + (y1 * src_width + x0) * src_bytes_per_pixel;
    const uint8_t *p11 = src + (y1 * src_width + x1) * src_bytes_per_pixel;
    
    // Interpolate each channel
    for (uint32_t c = 0; c < dst_bytes_per_pixel; c++) {
        float val = (p00[c] * fx1 + p01[c] * fx) * fy1 +
                    (p10[c] * fx1 + p11[c] * fx) * fy;
        dst[c] = (uint8_t)MAX(0, MIN(255, (int)roundf(val)));
    }
}

aicam_result_t image_resize(
    const uint8_t *src_image,
    uint32_t src_width,
    uint32_t src_height,
    uint32_t src_format,
    uint8_t *dst_image,
    uint32_t dst_width,
    uint32_t dst_height,
    uint32_t dst_format
)
{
    if (!src_image || !dst_image || src_width == 0 || src_height == 0 || 
        dst_width == 0 || dst_height == 0) {
        LOG_DRV_ERROR("Invalid parameters for image_resize");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    int src_bpp = DMA2D_BYTES_PER_PIXEL(src_format);
    int dst_bpp = DMA2D_BYTES_PER_PIXEL(dst_format);
    
    if (src_bpp == 0 || dst_bpp == 0) {
        LOG_DRV_ERROR("Unsupported pixel format for resize");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Calculate scale factors
    float scale_x = (float)src_width / dst_width;
    float scale_y = (float)src_height / dst_height;
    
    // Resize using bilinear interpolation
    for (uint32_t dy = 0; dy < dst_height; dy++) {
        for (uint32_t dx = 0; dx < dst_width; dx++) {
            // Map destination coordinates to source coordinates
            float sx = (dx + 0.5f) * scale_x - 0.5f;
            float sy = (dy + 0.5f) * scale_y - 0.5f;
            
            uint8_t *dst_pixel = dst_image + (dy * dst_width + dx) * dst_bpp;
            bilinear_interpolate_pixel(src_image, src_width, src_height, src_bpp,
                                      sx, sy, dst_pixel, dst_bpp);
        }
    }
    
    return AICAM_OK;
}

aicam_result_t image_crop_roi(
    const uint8_t *src_image,
    uint32_t src_width,
    uint32_t src_height,
    uint32_t src_format,
    uint32_t roi_x,
    uint32_t roi_y,
    uint32_t roi_width,
    uint32_t roi_height,
    uint8_t *dst_buffer,
    uint32_t dst_format
)
{
    if (!src_image || !dst_buffer) {
        LOG_DRV_ERROR("Invalid parameters for image_crop_roi");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Boundary check
    if (roi_x + roi_width > src_width) {
        roi_width = src_width - roi_x;
    }
    if (roi_y + roi_height > src_height) {
        roi_height = src_height - roi_y;
    }
    
    if (roi_width == 0 || roi_height == 0) {
        LOG_DRV_ERROR("Invalid ROI dimensions");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Get draw device
    device_t *draw_dev = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (!draw_dev) {
        LOG_DRV_ERROR("Draw device not found");
        return AICAM_ERROR_NOT_FOUND;
    }
    
    // Set color mode for draw device
    draw_colormode_param_t colormode = {
        .in_colormode = src_format,
        .out_colormode = dst_format
    };
    int ret = device_ioctl(draw_dev, DRAW_CMD_SET_COLOR_MODE, 
                          (uint8_t *)&colormode, sizeof(colormode));
    if (ret != AICAM_OK) {
        LOG_DRV_ERROR("Failed to set color mode");
        return ret;
    }
    
    // Calculate bytes per pixel for source address calculation
    int src_bpp = DMA2D_BYTES_PER_PIXEL(src_format);
    if (src_bpp == 0) {
        LOG_DRV_ERROR("Unsupported source pixel format");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Calculate source address offset for ROI
    uint8_t *src_roi_start = (uint8_t *)src_image + (roi_y * src_width + roi_x) * src_bpp;
    
    // Use DMA2D copy to crop ROI (copy from ROI start to destination)
    // Note: We use DRAW_CMD_COPY which copies entire source buffer, but we've calculated
    // the correct source offset, so it will copy from ROI start
    draw_copy_param_t copy_param = {
        .p_src = src_roi_start,
        .src_width = roi_width,
        .src_height = roi_height,
        .p_dst = dst_buffer,
        .dst_width = roi_width,
        .dst_height = roi_height,
        .x_offset = 0,
        .y_offset = 0
    };
    
    ret = device_ioctl(draw_dev, DRAW_CMD_COPY, (uint8_t *)&copy_param, sizeof(copy_param));
    if (ret != AICAM_OK) {
        LOG_DRV_ERROR("DMA2D crop failed: %d", ret);
        return ret;
    }
    
    return AICAM_OK;
}

aicam_result_t image_extract_and_resize_roi(
    const uint8_t *src_image,
    uint32_t src_width,
    uint32_t src_height,
    uint32_t src_format,
    const od_detect_t *bbox,
    uint32_t dst_width,
    uint32_t dst_height,
    uint32_t dst_format,
    uint8_t **dst_buffer,
    uint32_t *dst_size
)
{
    if (!src_image || !bbox || !dst_buffer || !dst_size) {
        LOG_DRV_ERROR("Invalid parameters for image_extract_and_resize_roi");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // 1. Convert normalized coordinates to pixel coordinates
    uint32_t roi_x = (uint32_t)(bbox->x * src_width);
    uint32_t roi_y = (uint32_t)(bbox->y * src_height);
    uint32_t roi_w = (uint32_t)(bbox->width * src_width);
    uint32_t roi_h = (uint32_t)(bbox->height * src_height);
    
    // Boundary check and clamp
    if (roi_x >= src_width || roi_y >= src_height) {
        LOG_DRV_ERROR("ROI out of bounds");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (roi_x + roi_w > src_width) {
        roi_w = src_width - roi_x;
    }
    if (roi_y + roi_h > src_height) {
        roi_h = src_height - roi_y;
    }
    
    if (roi_w == 0 || roi_h == 0) {
        LOG_DRV_ERROR("Invalid ROI dimensions after clamping");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // 2. Calculate buffer sizes
    int src_bpp = DMA2D_BYTES_PER_PIXEL(src_format);
    int dst_bpp = DMA2D_BYTES_PER_PIXEL(dst_format);
    
    if (src_bpp == 0 || dst_bpp == 0) {
        LOG_DRV_ERROR("Unsupported pixel format");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    uint32_t temp_size = roi_w * roi_h * src_bpp;
    uint32_t final_size = dst_width * dst_height * dst_bpp;
    
    // 3. Allocate temporary buffer for cropped ROI
    uint8_t *temp_buffer = hal_mem_alloc_large(temp_size);
    if (!temp_buffer) {
        LOG_DRV_ERROR("Failed to allocate temporary buffer for ROI");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // 4. Crop ROI using DMA2D hardware
    aicam_result_t ret = image_crop_roi(src_image, src_width, src_height, src_format,
                                       roi_x, roi_y, roi_w, roi_h,
                                       temp_buffer, src_format);
    if (ret != AICAM_OK) {
        LOG_DRV_ERROR("Failed to crop ROI: %d", ret);
        hal_mem_free(temp_buffer);
        return ret;
    }
    
    // 5. Allocate destination buffer
    *dst_buffer = hal_mem_alloc_large(final_size);
    if (!*dst_buffer) {
        LOG_DRV_ERROR("Failed to allocate destination buffer");
        hal_mem_free(temp_buffer);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // 6. Resize cropped ROI to target dimensions
    ret = image_resize(temp_buffer, roi_w, roi_h, src_format,
                      *dst_buffer, dst_width, dst_height, dst_format);
    
    // Free temporary buffer
    hal_mem_free(temp_buffer);
    
    if (ret != AICAM_OK) {
        LOG_DRV_ERROR("Failed to resize ROI: %d", ret);
        hal_mem_free(*dst_buffer);
        *dst_buffer = NULL;
        return ret;
    }
    
    *dst_size = final_size;
    return AICAM_OK;
}
