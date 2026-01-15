#ifndef __SL_RSI_BLE_H__
#define __SL_RSI_BLE_H__

#ifdef __cplusplus
    extern "C" {
#endif

#ifdef SLI_SI91X_ENABLE_BLE

#include <stdint.h>
#include <stddef.h>
#include "cmsis_os2.h"
#include "aicam_types.h"
#include "rsi_ble_apis.h"

#define SL_BLE_SCAN_RESULT_MAX_COUNT                64
#define SL_BLE_SCAN_REPLACE_TIMEOUT_MS              30000  ///< Replace timeout (unit: 1 ms), when buffer is full, replace oldest device if its age exceeds this timeout, 0 means disable replacement

// Connection management
#define SL_BLE_MAX_CONNECTIONS                      4       ///< Maximum number of concurrent BLE connections
#define SL_BLE_MAX_SERVICES_PER_DEVICE              16      ///< Maximum number of services per device
#define SL_BLE_MAX_CHARACTERISTICS_PER_SERVICE      16      ///< Maximum number of characteristics per service
#define SL_BLE_CONNECTION_TIMEOUT_MS                30000   ///< Connection timeout in milliseconds
#define SL_BLE_DISCOVERY_TIMEOUT_MS                 10000   ///< Service/characteristic discovery timeout in milliseconds

/// @brief BLE device information
typedef struct
{
    uint8_t addr_type;      ///< Address type.
    uint8_t addr[6];        ///< Address.
} sl_ble_addr_t;
/// @brief BLE scan device information
typedef struct
{
    uint8_t addr_type;      ///< Address type.
    uint8_t addr[6];        ///< Address.
    int8_t rssi;            ///< Received signal strength.
    uint8_t adv_type;       ///< Advertisement type.
    uint8_t name[31];       ///< Device name.
    uint32_t last_seen_tick; ///< Last seen system tick time.
} sl_ble_scan_info_t;
/// @brief BLE scan result
typedef struct
{
    uint8_t scan_count;                     ///< Number of available scan results
    sl_ble_scan_info_t *scan_info;          ///< Scan infos
} sl_ble_scan_result_t;
/// @brief BLE scan callback
typedef void (*sl_ble_scan_callback_t)(sl_ble_scan_info_t *scan_info);
/// @brief BLE scan configuration
typedef struct
{
    uint8_t scan_type;                      ///< Scan type (0x00 - passive, 0x01 - active)
    uint16_t scan_int;                      ///< Scan interval (0x0004 - 0xFFFF, unit: 0.625 ms)
    uint16_t scan_win;                      ///< Scan window (0x0004 - 0xFFFF, unit: 0.625 ms)
    uint32_t scan_duration;                 ///< Scan duration (unit: 1 ms), 0 means infinite
    int8_t rssi_threshold;                  ///< RSSI threshold (unit: dBm), only devices with RSSI >= threshold will be added, -127 means no filtering
    uint8_t accept_num;                     ///< Number of devices to accept
    sl_ble_addr_t *accept_list;             ///< List of devices to accept
    sl_ble_scan_callback_t callback;        ///< Scan callback
} sl_ble_scan_config_t;

/// @brief BLE scan start
/// @param[in] config Scan configuration
/// @return 0 on success, error code on failure
int sl_ble_scan_start(sl_ble_scan_config_t *config);

/// @brief BLE scan stop
/// @return 0 on success, error code on failure
int sl_ble_scan_stop(void);

/// @brief Get current BLE scan state
/// @return 1 if scanning, 0 otherwise
uint8_t sl_ble_is_scanning(void);

/// @brief BLE scan get result, must be called after scanning has stopped
/// @return Scan result
sl_ble_scan_result_t *sl_ble_scan_get_result(void);

/// @brief BLE printf scan result
/// @param[in] scan_result Scan result
void sl_ble_printf_scan_result(sl_ble_scan_result_t *scan_result);

/// @brief BLE test commands register
/// @param None
void sl_ble_test_commands_register(void);

// ========== Connection Management ==========

/// @brief BLE connection handle (opaque type)
typedef uint8_t sl_ble_conn_handle_t;

/// @brief Invalid connection handle
#define SL_BLE_INVALID_CONN_HANDLE                  0xFF

/// @brief BLE connection parameters
typedef struct {
    uint16_t conn_interval_min;      ///< Minimum connection interval (unit: 1.25 ms, range: 0x0006-0x0C80)
    uint16_t conn_interval_max;      ///< Maximum connection interval (unit: 1.25 ms, range: 0x0006-0x0C80)
    uint16_t conn_latency;           ///< Connection latency (range: 0x0000-0x01F3)
    uint16_t supervision_timeout;    ///< Supervision timeout (unit: 10 ms, range: 0x000A-0x0C80)
    uint16_t scan_interval;          ///< Scan interval for connection (unit: 0.625 ms, range: 0x0004-0x4000)
    uint16_t scan_window;            ///< Scan window for connection (unit: 0.625 ms, range: 0x0004-0x4000)
} sl_ble_conn_params_t;

/// @brief BLE connection status
typedef enum {
    SL_BLE_CONN_STATUS_DISCONNECTED = 0,
    SL_BLE_CONN_STATUS_CONNECTING,
    SL_BLE_CONN_STATUS_CONNECTED,
    SL_BLE_CONN_STATUS_DISCONNECTING
} sl_ble_conn_status_t;

/// @brief BLE service information
typedef struct {
    uuid_t uuid;                     ///< Service UUID
    uint16_t start_handle;            ///< Service start handle
    uint16_t end_handle;             ///< Service end handle
    uint8_t char_count;              ///< Number of characteristics discovered
} sl_ble_service_t;

/// @brief BLE characteristic properties
typedef enum {
    SL_BLE_CHAR_PROP_READ            = 0x02,
    SL_BLE_CHAR_PROP_WRITE           = 0x08,
    SL_BLE_CHAR_PROP_WRITE_NO_RESP   = 0x04,
    SL_BLE_CHAR_PROP_NOTIFY          = 0x10,
    SL_BLE_CHAR_PROP_INDICATE        = 0x20
} sl_ble_char_prop_t;

/// @brief BLE characteristic information
typedef struct {
    uuid_t uuid;                     ///< Characteristic UUID
    uint16_t handle;                 ///< Characteristic value handle
    uint16_t decl_handle;            ///< Characteristic declaration handle
    uint16_t cccd_handle;            ///< Client Characteristic Configuration Descriptor handle (0 if not found)
    uint8_t properties;              ///< Characteristic properties (bitmask)
} sl_ble_characteristic_t;

/// @brief BLE connected device information
typedef struct {
    sl_ble_conn_handle_t handle;     ///< Connection handle
    sl_ble_addr_t addr;              ///< Device address
    sl_ble_conn_status_t status;     ///< Connection status
    sl_ble_conn_params_t conn_params; ///< Connection parameters
    uint16_t mtu;                    ///< MTU size
    uint8_t service_count;           ///< Number of discovered services
    sl_ble_service_t services[SL_BLE_MAX_SERVICES_PER_DEVICE]; ///< Service list
    uint32_t connect_time;           ///< Connection timestamp
} sl_ble_connected_device_t;

/// @brief BLE connection configuration
typedef struct {
    sl_ble_conn_params_t *conn_params; ///< Connection parameters (NULL for default)
    uint32_t timeout_ms;                ///< Connection timeout in milliseconds (0 for default)
} sl_ble_conn_config_t;

/// @brief BLE service discovery filter
typedef struct {
    uuid_t *uuid_filter;              ///< UUID filter (NULL for no filter)
    uint16_t start_handle;            ///< Start handle (0 for auto)
    uint16_t end_handle;               ///< End handle (0 for auto)
} sl_ble_service_filter_t;

/// @brief BLE characteristic discovery filter
typedef struct {
    uuid_t *uuid_filter;              ///< UUID filter (NULL for no filter)
    uint16_t service_start_handle;    ///< Service start handle
    uint16_t service_end_handle;       ///< Service end handle
} sl_ble_char_filter_t;

/// @brief BLE notification callback
typedef void (*sl_ble_notify_callback_t)(sl_ble_conn_handle_t conn_handle, 
                                          uint16_t char_handle, 
                                          const uint8_t *data, 
                                          uint16_t data_len);

/// @brief Connect to a BLE device from scan result
/// @param[in] scan_info Scan result information
/// @param[in] config Connection configuration (NULL for default)
/// @param[out] conn_handle Output connection handle
/// @return 0 on success, error code on failure
int sl_ble_connect(sl_ble_scan_info_t *scan_info, 
                   sl_ble_conn_config_t *config, 
                   sl_ble_conn_handle_t *conn_handle);

/// @brief Disconnect from a BLE device
/// @param[in] conn_handle Connection handle
/// @return 0 on success, error code on failure
int sl_ble_disconnect(sl_ble_conn_handle_t conn_handle);

/// @brief Discover services on a connected device
/// @param[in] conn_handle Connection handle
/// @param[in] filter Service filter (NULL for no filter)
/// @param[out] services Output service list
/// @param[in,out] service_count Input: max services, Output: actual service count
/// @return 0 on success, error code on failure
int sl_ble_discover_services(sl_ble_conn_handle_t conn_handle,
                              sl_ble_service_filter_t *filter,
                              sl_ble_service_t *services,
                              uint8_t *service_count);

/// @brief Discover characteristics for a service
/// @param[in] conn_handle Connection handle
/// @param[in] service Service information
/// @param[in] filter Characteristic filter (NULL for no filter)
/// @param[out] characteristics Output characteristic list
/// @param[in,out] char_count Input: max characteristics, Output: actual characteristic count
/// @return 0 on success, error code on failure
int sl_ble_discover_characteristics(sl_ble_conn_handle_t conn_handle,
                                     sl_ble_service_t *service,
                                     sl_ble_char_filter_t *filter,
                                     sl_ble_characteristic_t *characteristics,
                                     uint8_t *char_count);

/// @brief Enable notifications for a characteristic
/// @param[in] conn_handle Connection handle
/// @param[in] characteristic Characteristic information
/// @param[in] callback Notification callback (NULL to disable)
/// @return 0 on success, error code on failure
int sl_ble_enable_notify(sl_ble_conn_handle_t conn_handle,
                         sl_ble_characteristic_t *characteristic,
                         sl_ble_notify_callback_t callback);

/// @brief Write to a characteristic
/// @param[in] conn_handle Connection handle
/// @param[in] characteristic Characteristic information
/// @param[in] data Data to write
/// @param[in] data_len Data length
/// @param[in] write_without_response Use write without response if supported
/// @return 0 on success, error code on failure
int sl_ble_write_characteristic(sl_ble_conn_handle_t conn_handle,
                                 sl_ble_characteristic_t *characteristic,
                                 const uint8_t *data,
                                 uint16_t data_len,
                                 uint8_t write_without_response);

/// @brief Read from a characteristic
/// @param[in] conn_handle Connection handle
/// @param[in] characteristic Characteristic information
/// @param[out] data Output buffer
/// @param[in,out] data_len Input: buffer size, Output: actual data length
/// @return 0 on success, error code on failure
int sl_ble_read_characteristic(sl_ble_conn_handle_t conn_handle,
                                sl_ble_characteristic_t *characteristic,
                                uint8_t *data,
                                uint16_t *data_len);

/// @brief Get connected device information
/// @param[in] conn_handle Connection handle
/// @param[out] device Output device information
/// @return 0 on success, error code on failure
int sl_ble_get_connected_device(sl_ble_conn_handle_t conn_handle,
                                 sl_ble_connected_device_t *device);

/// @brief Check if device is connected
/// @param[in] conn_handle Connection handle
/// @return 1 if connected, 0 otherwise
uint8_t sl_ble_is_connected(sl_ble_conn_handle_t conn_handle);

/// @brief Get number of connected devices
/// @return Number of connected devices
uint8_t sl_ble_connected_num(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* __SL_RSI_BLE_H__ */
