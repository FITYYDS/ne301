/**
 ******************************************************************************
 * @file    image_utils.h
 * @author  GPM Application Team
 * @brief   Image processing utilities for AI model cascade (crop and resize)
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

#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

#include <stdint.h>
#include "aicam_error.h"
#include "aicam_types.h"
#include "pp.h"  // For od_detect_t structure

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Extract ROI from source image and resize to target dimensions
 * 
 * This function is designed for model cascade scenarios (e.g., detection -> recognition):
 * 1. Crop the ROI region from source image based on bounding box (normalized coordinates 0-1)
 * 2. Resize the cropped ROI to target dimensions for next model input
 * 
 * @param src_image Source image buffer
 * @param src_width Source image width (pixels)
 * @param src_height Source image height (pixels)
 * @param src_format Source image pixel format (DMA2D format, e.g., DMA2D_INPUT_RGB888)
 * @param bbox Bounding box with normalized coordinates [0,1] (x, y, width, height)
 * @param dst_width Target width for resized ROI (pixels)
 * @param dst_height Target height for resized ROI (pixels)
 * @param dst_format Destination image pixel format (DMA2D format)
 * @param dst_buffer Output buffer pointer (will be allocated, caller must free)
 * @param dst_size Output buffer size (bytes, returned)
 * @return aicam_result_t AICAM_OK on success, error code on failure
 */
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
);

/**
 * @brief Crop ROI from source image using DMA2D hardware
 * 
 * @param src_image Source image buffer
 * @param src_width Source image width (pixels)
 * @param src_height Source image height (pixels)
 * @param src_format Source image pixel format (DMA2D format)
 * @param roi_x ROI X position in source image (pixels)
 * @param roi_y ROI Y position in source image (pixels)
 * @param roi_width ROI width (pixels)
 * @param roi_height ROI height (pixels)
 * @param dst_buffer Output buffer (must be pre-allocated, size = roi_width * roi_height * bytes_per_pixel)
 * @param dst_format Destination image pixel format (DMA2D format)
 * @return aicam_result_t AICAM_OK on success, error code on failure
 */
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
);

/**
 * @brief Resize image using CPU bilinear interpolation
 * 
 * @param src_image Source image buffer
 * @param src_width Source image width (pixels)
 * @param src_height Source image height (pixels)
 * @param src_format Source image pixel format (DMA2D format)
 * @param dst_image Destination image buffer (must be pre-allocated)
 * @param dst_width Destination image width (pixels)
 * @param dst_height Destination image height (pixels)
 * @param dst_format Destination image pixel format (DMA2D format)
 * @return aicam_result_t AICAM_OK on success, error code on failure
 */
aicam_result_t image_resize(
    const uint8_t *src_image,
    uint32_t src_width,
    uint32_t src_height,
    uint32_t src_format,
    uint8_t *dst_image,
    uint32_t dst_width,
    uint32_t dst_height,
    uint32_t dst_format
);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_UTILS_H */
