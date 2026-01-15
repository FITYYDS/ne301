/**
 * @file rtmp_service.h
 * @brief RTMP Service Interface Header
 * @details RTMP streaming service interface definition, supports live video push streaming
 */

#ifndef RTMP_SERVICE_H
#define RTMP_SERVICE_H

#include "aicam_types.h"
#include "service_interfaces.h"
#include "rtmp_publisher.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Constants ==================== */

#define RTMP_SERVICE_VERSION        "1.0.0"
#define RTMP_MAX_URL_LENGTH         256
#define RTMP_MAX_STREAM_KEY_LENGTH  128
#define RTMP_MAX_CALLBACKS          4

/* ==================== RTMP Service Types ==================== */

/**
 * @brief RTMP service runtime configuration (simplified)
 */
typedef struct {
    char url[RTMP_MAX_URL_LENGTH];               ///< RTMP server URL
    char stream_key[RTMP_MAX_STREAM_KEY_LENGTH]; ///< Stream key
    aicam_bool_t auto_reconnect;                 ///< Auto reconnect on disconnect
    uint32_t reconnect_interval_ms;              ///< Reconnect interval (default: 5000)
    uint32_t max_reconnect_attempts;             ///< Max reconnect attempts (0 = infinite)
    aicam_bool_t async_send;                     ///< Use async queue (default: true), false for sync send
} rtmp_service_config_t;

/**
 * @brief RTMP stream state
 */
typedef enum {
    RTMP_STREAM_STATE_IDLE = 0,         ///< Not streaming
    RTMP_STREAM_STATE_CONNECTING,       ///< Connecting to server
    RTMP_STREAM_STATE_STREAMING,        ///< Actively streaming
    RTMP_STREAM_STATE_RECONNECTING,     ///< Reconnecting after disconnect
    RTMP_STREAM_STATE_STOPPING,         ///< Stopping stream
    RTMP_STREAM_STATE_ERROR             ///< Error state
} rtmp_stream_state_t;

/**
 * @brief RTMP event types
 */
typedef enum {
    RTMP_EVENT_CONNECTED = 0,           ///< Connected to server
    RTMP_EVENT_DISCONNECTED,            ///< Disconnected from server
    RTMP_EVENT_STREAM_STARTED,          ///< Stream started
    RTMP_EVENT_STREAM_STOPPED,          ///< Stream stopped
    RTMP_EVENT_RECONNECTING,            ///< Reconnecting
    RTMP_EVENT_RECONNECT_FAILED,        ///< Reconnect failed (max attempts reached)
    RTMP_EVENT_ERROR                    ///< Error occurred
} rtmp_event_type_t;

/**
 * @brief RTMP service statistics
 */
typedef struct {
    uint64_t frames_sent;               ///< Total frames sent
    uint64_t bytes_sent;                ///< Total bytes sent
    uint64_t keyframes_sent;            ///< Total keyframes sent
    uint64_t dropped_frames;            ///< Dropped frames count
    uint64_t errors;                    ///< Error count
    uint32_t reconnect_count;           ///< Reconnect attempts
    uint32_t current_bitrate_kbps;      ///< Current bitrate (calculated)
    uint32_t avg_frame_size;            ///< Average frame size
    uint32_t stream_duration_sec;       ///< Stream duration in seconds
    uint32_t last_error_code;           ///< Last error code
    uint32_t stream_start_time;         ///< Stream start timestamp
} rtmp_service_stats_t;

/**
 * @brief RTMP event data
 */
typedef struct {
    rtmp_event_type_t event;            ///< Event type
    int error_code;                     ///< Error code (for error events)
    const char *error_message;          ///< Error message (for error events)
    uint32_t reconnect_attempt;         ///< Current reconnect attempt number
} rtmp_event_data_t;

/**
 * @brief RTMP event callback function type
 */
typedef void (*rtmp_event_callback_t)(const rtmp_event_data_t *event_data, void *user_data);

/* ==================== Service Interface Functions ==================== */

/**
 * @brief Initialize RTMP service
 * @param config Service configuration (optional, uses defaults if NULL)
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_init(void *config);

/**
 * @brief Start RTMP service
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_start(void);

/**
 * @brief Stop RTMP service
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_stop(void);

/**
 * @brief Deinitialize RTMP service
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_deinit(void);

/**
 * @brief Get RTMP service state
 * @return service_state_t Service state
 */
service_state_t rtmp_service_get_state(void);

/* ==================== Stream Control ==================== */

/**
 * @brief Start streaming
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_start_stream(void);

/**
 * @brief Stop streaming
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_stop_stream(void);

/**
 * @brief Restart streaming (stop and start)
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_restart_stream(void);

/**
 * @brief Check if currently streaming
 * @return aicam_bool_t TRUE if streaming
 */
aicam_bool_t rtmp_service_is_streaming(void);

/**
 * @brief Get stream state
 * @return rtmp_stream_state_t Current stream state
 */
rtmp_stream_state_t rtmp_service_get_stream_state(void);

/**
 * @brief Get stream state as string
 * @param state Stream state
 * @return const char* State string
 */
const char* rtmp_stream_state_to_string(rtmp_stream_state_t state);

/* ==================== Configuration ==================== */

/**
 * @brief Get default configuration
 * @param config Configuration structure to fill
 */
void rtmp_service_get_default_config(rtmp_service_config_t *config);

/**
 * @brief Get current configuration
 * @param config Configuration structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_get_config(rtmp_service_config_t *config);

/**
 * @brief Set configuration
 * @param config Configuration to apply
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_set_config(const rtmp_service_config_t *config);

/**
 * @brief Set RTMP URL
 * @param url RTMP server URL
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_set_url(const char *url);

/**
 * @brief Set stream key
 * @param stream_key Stream key
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_set_stream_key(const char *stream_key);

/**
 * @brief Save configuration to NVS
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_save_config(void);

/**
 * @brief Load configuration from NVS
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_load_config(void);

/* ==================== Statistics ==================== */

/**
 * @brief Get service statistics
 * @param stats Statistics structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_get_stats(rtmp_service_stats_t *stats);

/**
 * @brief Reset statistics
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_reset_stats(void);

/* ==================== Event Management ==================== */

/**
 * @brief Register event callback
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_register_callback(rtmp_event_callback_t callback, void *user_data);

/**
 * @brief Unregister event callback
 * @param callback Callback function to remove
 * @return aicam_result_t Operation result
 */
aicam_result_t rtmp_service_unregister_callback(rtmp_event_callback_t callback);

/* ==================== Utility Functions ==================== */

/**
 * @brief Get service version
 * @return const char* Version string
 */
const char* rtmp_service_get_version(void);

/**
 * @brief Check if service is initialized
 * @return aicam_bool_t TRUE if initialized
 */
aicam_bool_t rtmp_service_is_initialized(void);

/**
 * @brief Check if service is running
 * @return aicam_bool_t TRUE if running
 */
aicam_bool_t rtmp_service_is_running(void);

/* ==================== CLI Commands ==================== */

/**
 * @brief Register RTMP CLI commands
 */
void rtmp_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif /* RTMP_SERVICE_H */
