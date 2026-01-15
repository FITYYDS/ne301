/**
 * @file api_preview_module.h
 * @brief Preview API Module Header
 * @details Video preview service API module for WebSocket streaming control
 */

#ifndef API_PREVIEW_MODULE_H
#define API_PREVIEW_MODULE_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register Preview API module
 * @return aicam_result_t Operation result
 */
aicam_result_t web_api_register_preview_module(void);

#ifdef __cplusplus
}
#endif

#endif /* API_PREVIEW_MODULE_H */

