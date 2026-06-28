/**
 * @file api_rtsp_module.h
 * @brief RTSP API Module Header
 * @details RTSP server configuration and control API module
 */

#ifndef API_RTSP_MODULE_H
#define API_RTSP_MODULE_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register RTSP API module
 * @return aicam_result_t Operation result
 */
aicam_result_t web_api_register_rtsp_module(void);

#ifdef __cplusplus
}
#endif

#endif /* API_RTSP_MODULE_H */
