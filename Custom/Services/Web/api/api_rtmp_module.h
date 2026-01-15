/**
 * @file api_rtmp_module.h
 * @brief RTMP API Module Header
 * @details RTMP streaming service API module for configuration and control
 */

#ifndef API_RTMP_MODULE_H
#define API_RTMP_MODULE_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register RTMP API module
 * @return aicam_result_t Operation result
 */
aicam_result_t web_api_register_rtmp_module(void);

#ifdef __cplusplus
}
#endif

#endif /* API_RTMP_MODULE_H */
