/**
 * @file api_isp_module.h
 * @brief ISP API Module Header - HTTP interface for ISP parameter tuning
 * @details Parameter get/set interface based on STM32 ISP library
 */

#ifndef API_ISP_MODULE_H
#define API_ISP_MODULE_H

#include "aicam_types.h"
#include "web_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== ISP Business Error Codes (2001-2099) ==================== */

typedef enum {
    API_ISP_ERROR_NOT_INITIALIZED     = 2001,  ///< ISP not initialized
    API_ISP_ERROR_INVALID_PARAM       = 2002,  ///< ISP parameter is invalid
    API_ISP_ERROR_PARAM_OUT_OF_RANGE  = 2003,  ///< ISP parameter is out of range
    API_ISP_ERROR_DEPENDENCY_ERROR    = 2004,  ///< ISP parameter dependency error
    API_ISP_ERROR_HAL_ERROR           = 2005,  ///< ISP hardware abstraction layer error
    API_ISP_ERROR_SENSOR_ERROR        = 2006,  ///< Sensor error
    API_ISP_ERROR_ALGO_ERROR          = 2007,  ///< ISP algorithm error
    API_ISP_ERROR_BUSY                = 2008,  ///< ISP is busy
} api_isp_error_code_t;

/* ==================== ISP Parameter Constraints ==================== */

/** Demosaicing strength max value */
#define ISP_API_DEMOS_STRENGTH_MAX          7
/** Bad pixel strength max value */
#define ISP_API_BADPIXEL_STRENGTH_MAX       7
/** Black level max value */
#define ISP_API_BLACKLEVEL_MAX              255
/** Contrast LUT coefficient max value */
#define ISP_API_CONTRAST_COEFF_MAX          394
/** ISP gain precision factor (100000000 = 1.0x) */
#define ISP_API_GAIN_PRECISION              100000000
/** ISP gain max value (16.0x) */
#define ISP_API_GAIN_MAX                    1600000000
/** Color conversion coefficient max value (4.0x) */
#define ISP_API_COLORCONV_MAX               399000000
/** Stat window min size */
#define ISP_API_STATWINDOW_MIN              4
/** Stat window max size */
#define ISP_API_STATWINDOW_MAX              4094

/* ==================== Module Initialization ==================== */

/**
 * @brief Initialize ISP API module
 * @return Operation result
 */
aicam_result_t api_isp_module_init(void);

/**
 * @brief Register ISP API module to web service
 * @return Operation result
 */
aicam_result_t web_api_register_isp_module(void);

/**
 * @brief Deinitialize ISP API module
 * @return Operation result
 */
aicam_result_t api_isp_module_deinit(void);

/* ==================== Global ISP Parameters ==================== */

/**
 * @brief Get all ISP parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_all_params(http_handler_context_t *ctx);

/**
 * @brief Set multiple ISP parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_params(http_handler_context_t *ctx);

/**
 * @brief Reset ISP parameters to default
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_reset_params(http_handler_context_t *ctx);

/* ==================== Sensor Information ==================== */

/**
 * @brief Get sensor information
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_sensor_info(http_handler_context_t *ctx);

/**
 * @brief Get ISP statistics
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_statistics(http_handler_context_t *ctx);

/* ==================== AEC (Auto Exposure Control) ==================== */

/**
 * @brief Get AEC parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_aec(http_handler_context_t *ctx);

/**
 * @brief Set AEC parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_aec(http_handler_context_t *ctx);

/**
 * @brief Get manual exposure parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_manual_exposure(http_handler_context_t *ctx);

/**
 * @brief Set manual exposure parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_manual_exposure(http_handler_context_t *ctx);

/* ==================== AWB (Auto White Balance) ==================== */

/**
 * @brief Get AWB parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_awb(http_handler_context_t *ctx);

/**
 * @brief Set AWB parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_awb(http_handler_context_t *ctx);

/* ==================== Demosaicing ==================== */

/**
 * @brief Get demosaicing parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_demosaicing(http_handler_context_t *ctx);

/**
 * @brief Set demosaicing parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_demosaicing(http_handler_context_t *ctx);

/* ==================== Black Level ==================== */

/**
 * @brief Get black level parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_black_level(http_handler_context_t *ctx);

/**
 * @brief Set black level parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_black_level(http_handler_context_t *ctx);

/* ==================== Bad Pixel Correction ==================== */

/**
 * @brief Get bad pixel parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_bad_pixel(http_handler_context_t *ctx);

/**
 * @brief Set bad pixel parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_bad_pixel(http_handler_context_t *ctx);

/* ==================== ISP Gain ==================== */

/**
 * @brief Get ISP gain parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_gain(http_handler_context_t *ctx);

/**
 * @brief Set ISP gain parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_gain(http_handler_context_t *ctx);

/* ==================== Color Conversion ==================== */

/**
 * @brief Get color conversion matrix
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_color_conv(http_handler_context_t *ctx);

/**
 * @brief Set color conversion matrix
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_color_conv(http_handler_context_t *ctx);

/* ==================== Contrast ==================== */

/**
 * @brief Get contrast parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_contrast(http_handler_context_t *ctx);

/**
 * @brief Set contrast parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_contrast(http_handler_context_t *ctx);

/* ==================== Gamma ==================== */

/**
 * @brief Get gamma parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_gamma(http_handler_context_t *ctx);

/**
 * @brief Set gamma parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_gamma(http_handler_context_t *ctx);

/* ==================== Stat Removal ==================== */

/**
 * @brief Get stat removal parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_stat_removal(http_handler_context_t *ctx);

/**
 * @brief Set stat removal parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_stat_removal(http_handler_context_t *ctx);

/* ==================== Statistics Area ==================== */

/**
 * @brief Get statistics area parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_stat_area(http_handler_context_t *ctx);

/**
 * @brief Set statistics area parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_stat_area(http_handler_context_t *ctx);

/* ==================== Sensor Delay ==================== */

/**
 * @brief Get sensor delay parameter
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_sensor_delay(http_handler_context_t *ctx);

/**
 * @brief Set sensor delay parameter
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_sensor_delay(http_handler_context_t *ctx);

/* ==================== Lux Reference ==================== */

/**
 * @brief Get lux reference parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_get_lux_ref(http_handler_context_t *ctx);

/**
 * @brief Set lux reference parameters
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_set_lux_ref(http_handler_context_t *ctx);

/* ==================== Helper Functions ==================== */

/**
 * @brief Convert ISP status to API error code
 * @param isp_status ISP status code
 * @return API error code
 */
api_isp_error_code_t api_isp_status_to_error(int isp_status);

/**
 * @brief Get ISP error message
 * @param error_code API ISP error code
 * @return Error message string
 */
const char* api_isp_error_message(api_isp_error_code_t error_code);

/* ==================== Config Save/Load ==================== */

/**
 * @brief Save current ISP configuration to NVS
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_save_config(http_handler_context_t *ctx);

/**
 * @brief Load and apply ISP configuration from NVS
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_load_config(http_handler_context_t *ctx);

/* ==================== Config Export/Import ==================== */

/**
 * @brief Export current ISP configuration as JSON
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_export_config(http_handler_context_t *ctx);

/**
 * @brief Import and apply ISP configuration from JSON
 * @param ctx HTTP handler context
 * @return Operation result
 */
aicam_result_t api_isp_import_config(http_handler_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* API_ISP_MODULE_H */
