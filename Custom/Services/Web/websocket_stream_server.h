/**
 * @file websocket_stream_server.h
 * @brief WebSocket Stream Server Header
 * @details WebSocket server header based on mongoose library.
 */

#ifndef WEBSOCKET_STREAM_SERVER_H
#define WEBSOCKET_STREAM_SERVER_H

#include "aicam_types.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Byte Order Conversion ==================== */

// Byte order conversion macros
#define WS_HTONL(x) ((((x) & 0xFF000000) >> 24) | \
                     (((x) & 0x00FF0000) >> 8)  | \
                     (((x) & 0x0000FF00) << 8)  | \
                     (((x) & 0x000000FF) << 24))

#define WS_HTONS(x) ((((x) & 0xFF00) >> 8) | \
                     (((x) & 0x00FF) << 8))

#define WS_HTONLL(x) ((((x) & 0xFF00000000000000ULL) >> 56) | \
                      (((x) & 0x00FF000000000000ULL) >> 40) | \
                      (((x) & 0x0000FF0000000000ULL) >> 24) | \
                      (((x) & 0x000000FF00000000ULL) >> 8)  | \
                      (((x) & 0x00000000FF000000ULL) << 8)  | \
                      (((x) & 0x0000000000FF0000ULL) << 24) | \
                      (((x) & 0x000000000000FF00ULL) << 40) | \
                      (((x) & 0x00000000000000FFULL) << 56))

// Network byte order conversion (little endian to big endian)
#define WS_TO_NETWORK_32(x) WS_HTONL(x)
#define WS_TO_NETWORK_16(x) WS_HTONS(x)
#define WS_TO_NETWORK_64(x) WS_HTONLL(x)

// Host byte order conversion (big endian to little endian)
#define WS_FROM_NETWORK_32(x) WS_HTONL(x)
#define WS_FROM_NETWORK_16(x) WS_HTONS(x)
#define WS_FROM_NETWORK_64(x) WS_HTONLL(x)

/* ==================== Frame Packet Definitions ==================== */

#define WS_FRAME_MAGIC                  0x57534653  // "WSFS" - WebSocket Frame Stream 
#define WS_FRAME_VERSION                0x01        // Protocol version

/**
 * @brief WebSocket frame types
 */
typedef enum {
    WS_FRAME_TYPE_UNKNOWN = 0,
    WS_FRAME_TYPE_H264_KEY,           // H.264 key frame
    WS_FRAME_TYPE_H264_DELTA,         // H.264 delta frame
    WS_FRAME_TYPE_H265_KEY,           // H.265 key frame
    WS_FRAME_TYPE_H265_DELTA,         // H.265 delta frame
    WS_FRAME_TYPE_MJPEG,              // MJPEG frame
    WS_FRAME_TYPE_JPEG,               // JPEG frame
    WS_FRAME_TYPE_METADATA,           // Metadata
    WS_FRAME_TYPE_CONTROL             // Control frame
} websocket_frame_type_t;

/**
 * @brief WebSocket frame header structure (64 bytes) - Network byte order
 * @details 1-byte alignment enforced for cross-platform compatibility, all multi-byte fields use network byte order
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;                   // Magic number "WSFS" (network byte order)
    uint8_t version;                  // Protocol version
    uint8_t frame_type;               // Frame type
    uint16_t reserved;                // Reserved field (network byte order)
    uint64_t timestamp;               // Timestamp (microseconds, network byte order)
    uint32_t frame_size;              // Frame data size (network byte order)
    uint32_t stream_id;               // Stream ID (network byte order)
    uint32_t sequence;                // Sequence number (network byte order)
    uint32_t width;                   // Image width (network byte order)
    uint32_t height;                  // Image height (network byte order)
    uint32_t format;                  // Image format (network byte order)
    uint32_t flags;                   // Flags (network byte order)

    
    // Encoder information (extended fields)
    uint32_t coding_type;             // H.264 coding type (network byte order)
    uint32_t stream_size;             // Encoded stream size (network byte order)
    uint32_t num_nalus;               // Number of NAL units (network byte order)
    uint32_t mse_mul256;              // Mean Squared Error * 256 (network byte order)
    uint32_t header_size;             // Header size (network byte order)
} websocket_frame_header_t;
#pragma pack(pop)

/* ==================== Configuration Structures ==================== */

/**
 * @brief WebSocket stream server configuration
 */
typedef struct {
    uint16_t port;                    // Listen port
    uint32_t max_clients;             // Maximum number of clients
    uint32_t max_frame_size;          // Maximum frame size
    char stream_path[64];             // WebSocket listen path (e.g., "/stream")
    uint32_t task_priority;           // Server task priority
    uint32_t task_stack_size;         // Server task stack size (bytes)
    uint32_t ping_interval_ms;        // Ping interval in milliseconds (0 = disabled)
    uint32_t pong_timeout_ms;         // Pong timeout in milliseconds (0 = disabled)
} websocket_stream_config_t;

/**
 * @brief WebSocket stream server statistics
 */
typedef struct {
    uint32_t total_connections;       // Total connections
    uint32_t total_disconnections;    // Total disconnections
    uint32_t active_clients;          // Active clients
    uint64_t total_frames_sent;       // Total frames sent
    uint64_t total_bytes_sent;        // Total bytes sent
    uint32_t error_count;             // Error count
    uint64_t uptime_ms;               // Uptime (milliseconds)
    aicam_bool_t stream_active;       // Stream active status
    uint32_t stream_id;               // Current stream ID
    uint32_t stream_fps;              // Stream frame rate
} websocket_stream_stats_t;

/* ==================== API Function Declarations ==================== */

/**
 * @brief Initialize WebSocket stream server
 * @param config Server configuration
 * @return Operation result
 */
aicam_result_t websocket_stream_server_init(const websocket_stream_config_t *config);

/**
 * @brief Deinitialize WebSocket stream server
 * @return Operation result
 */
aicam_result_t websocket_stream_server_deinit(void);

/**
 * @brief Start WebSocket stream server
 * @return Operation result
 */
aicam_result_t websocket_stream_server_start(void);

/**
 * @brief Stop WebSocket stream server
 * @return Operation result
 */
aicam_result_t websocket_stream_server_stop(void);

/**
 * @brief Start video stream
 * @param stream_id Stream ID
 * @return Operation result
 */
aicam_result_t websocket_stream_server_start_stream(uint32_t stream_id);

/**
 * @brief Stop video stream
 * @return Operation result
 */
aicam_result_t websocket_stream_server_stop_stream(void);

/**
 * @brief Send video frame
 * @param frame_data Frame data
 * @param frame_size Frame size
 * @param timestamp Timestamp (microseconds)
 * @param frame_type Frame type
 * @param width Image width
 * @param height Image height
 * @return Operation result
 */
aicam_result_t websocket_stream_server_send_frame(const void *frame_data, size_t frame_size, 
                                                uint64_t timestamp, websocket_frame_type_t frame_type,
                                                uint32_t width, uint32_t height);

/**
 * @brief Send video frame with encoder information
 * @param frame_data Frame data
 * @param frame_size Frame size
 * @param timestamp Timestamp (microseconds)
 * @param frame_type Frame type
 * @param width Image width
 * @param height Image height
 * @param encoder_info Encoder output information
 * @return Operation result
 */
aicam_result_t websocket_stream_server_send_frame_with_encoder_info(const void *frame_data, size_t frame_size, 
                                                                   uint64_t timestamp, websocket_frame_type_t frame_type,
                                                                   uint32_t width, uint32_t height,
                                                                   const void *encoder_info);

/**
 * @brief Get server statistics
 * @param stats Statistics structure (output parameter)
 * @return Operation result
 */
aicam_result_t websocket_stream_server_get_stats(websocket_stream_stats_t *stats);

/**
 * @brief Get default configuration
 * @param config Configuration structure (output parameter)
 */
void websocket_stream_get_default_config(websocket_stream_config_t *config);

/**
 * @brief Check if server is initialized
 * @return Initialization status
 */
aicam_bool_t websocket_stream_server_is_initialized(void);

/**
 * @brief Check if server is running
 * @return Running status
 */
aicam_bool_t websocket_stream_server_is_running(void);

/**
 * @brief subscribe to Video Hub to receive encoded frames (Video Hub mode)
 * @return AICAM_OK success, other failure
 * @note When using Hub mode, you don't need to call send_frame, frames will be automatically received from the Hub
 */
aicam_result_t websocket_stream_server_subscribe_hub(void);

/**
 * @brief unsubscribe from Video Hub (Video Hub mode)
 * @return AICAM_OK success
 */
aicam_result_t websocket_stream_server_unsubscribe_hub(void);

/**
 * @brief Register WebSocket stream server debug commands
 */
void websocket_stream_server_register_commands(void);

#ifdef __cplusplus
}
#endif

#endif /* WEBSOCKET_STREAM_SERVER_H */