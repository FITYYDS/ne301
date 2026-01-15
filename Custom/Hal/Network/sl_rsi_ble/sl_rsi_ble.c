#ifdef SLI_SI91X_ENABLE_BLE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include "Log/debug.h"
#include "aicam_error.h"
#include "sl_constants.h"
#include "rsi_ble_apis.h"
#include "rsi_ble.h"
#include "rsi_bt_common.h"
#include "rsi_bt_common_apis.h"
#include "rsi_common_apis.h"
#include "sl_rsi_ble.h"
#include "ble_config.h"

#define SL_RSI_BLE_TICK_DIFF_MS(last, now)           ((now >= last) ? (now - last) : (osWaitForever - last + now))

// Internal scan state
typedef struct {
    volatile uint8_t is_scanning;
    sl_ble_scan_config_t config;  // Copy of config (accept_list not copied, only used during hardware setup)
    sl_ble_scan_info_t scan_results[SL_BLE_SCAN_RESULT_MAX_COUNT];
    uint8_t scan_count;
    osMutexId_t mutex;
    osSemaphoreId_t scan_sem;
    osThreadId_t scan_timeout_thread;
    uint32_t scan_start_time;
} sl_ble_scan_state_t;

static sl_ble_scan_state_t g_ble_scan_state = {0};

// Connection management state
typedef struct {
    sl_ble_connected_device_t devices[SL_BLE_MAX_CONNECTIONS];
    uint8_t used_handles[SL_BLE_MAX_CONNECTIONS];
    uint8_t handle_count;
    osMutexId_t mutex;
    osSemaphoreId_t conn_sem;
    osSemaphoreId_t discovery_sem;
    osSemaphoreId_t read_sem;
    osSemaphoreId_t write_sem;
    uint16_t pending_resp_id;
    uint16_t pending_resp_status;
    rsi_ble_resp_att_value_t pending_read_data;
    sl_ble_notify_callback_t notify_callbacks[SL_BLE_MAX_CONNECTIONS * SL_BLE_MAX_SERVICES_PER_DEVICE * SL_BLE_MAX_CHARACTERISTICS_PER_SERVICE];
} sl_ble_conn_state_t;

static sl_ble_conn_state_t g_ble_conn_state = {0};

// Forward declarations
static void sl_ble_adv_report_callback(rsi_ble_event_adv_report_t *adv_report);
static void sl_ble_scan_timeout_thread(void *arg);

// Connection management forward declarations
static void sl_ble_conn_init(void);
static void sl_ble_conn_status_callback(rsi_ble_event_conn_status_t *conn_status);
static void sl_ble_enhance_conn_status_callback(rsi_ble_event_enhance_conn_status_t *enhance_conn_status);
static void sl_ble_disconnect_callback(rsi_ble_event_disconnect_t *disconnect, uint16_t reason);
static void sl_ble_conn_update_complete_callback(rsi_ble_event_conn_update_t *conn_update, uint16_t resp_status);
static void sl_ble_profiles_list_resp_callback(uint16_t resp_status, rsi_ble_resp_profiles_list_t *profiles);
static void sl_ble_profile_resp_callback(uint16_t resp_status, profile_descriptors_t *profile);
static void sl_ble_char_services_resp_callback(uint16_t resp_status, rsi_ble_resp_char_services_t *char_services);
static void sl_ble_att_desc_resp_callback(uint16_t resp_status, rsi_ble_resp_att_descs_t *att_descs);
static void sl_ble_read_resp_callback(uint16_t resp_status, uint16_t resp_id, rsi_ble_resp_att_value_t *att_val);
static void sl_ble_write_resp_callback(uint16_t resp_status, uint16_t resp_id);
static void sl_ble_gatt_write_event_callback(uint16_t event_id, rsi_ble_event_write_t *write_event);
static sl_ble_connected_device_t *sl_ble_find_device_by_addr(uint8_t *addr, uint8_t addr_type);
static sl_ble_connected_device_t *sl_ble_find_device_by_handle(sl_ble_conn_handle_t handle);
static sl_ble_conn_handle_t sl_ble_alloc_conn_handle(void);
static void sl_ble_free_conn_handle(sl_ble_conn_handle_t handle);

// Check if device already exists in scan results
static int sl_ble_find_device(uint8_t *addr, uint8_t addr_type)
{
    for (int i = 0; i < g_ble_scan_state.scan_count; i++) {
        if (g_ble_scan_state.scan_results[i].addr_type == addr_type &&
            memcmp(g_ble_scan_state.scan_results[i].addr, addr, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static int sl_ble_find_timeout_device(void)
{
    uint32_t current_tick = osKernelGetTickCount();
    for (int i = 0; i < g_ble_scan_state.scan_count; i++) {
        if (SL_RSI_BLE_TICK_DIFF_MS(g_ble_scan_state.scan_results[i].last_seen_tick, current_tick) > SL_BLE_SCAN_REPLACE_TIMEOUT_MS) {
            return i;
        }
    }
    return -1;
}

// Add or update device in scan results
static void sl_ble_add_device(rsi_ble_event_adv_report_t *adv_report)
{
    // Check RSSI threshold if configured
    if (g_ble_scan_state.config.rssi_threshold > -127) {
        if (adv_report->rssi < g_ble_scan_state.config.rssi_threshold) {
            return; // RSSI below threshold, ignore this device
        }
    }

    int idx = sl_ble_find_device(adv_report->dev_addr, adv_report->dev_addr_type);

    if (idx >= 0) {
        // Update existing device
        sl_ble_scan_info_t *info = &g_ble_scan_state.scan_results[idx];

        // Update RSSI if better
        // if (adv_report->rssi > info->rssi) {
        //     info->rssi = adv_report->rssi;
        // }
        // update rssi to the latest value
        info->rssi = adv_report->rssi;
        info->last_seen_tick = osKernelGetTickCount();

        // Extract device name from advertisement data and update if valid & changed
        uint8_t new_name[sizeof(info->name)] = {0};
        BT_LE_ADPacketExtract(new_name, adv_report->adv_data, adv_report->adv_data_len);
        if (new_name[0] != '\0' && memcmp(new_name, info->name, sizeof(info->name)) != 0) {
            memcpy(info->name, new_name, sizeof(info->name));
        }
        return;
    }

    if (g_ble_scan_state.scan_count >= SL_BLE_SCAN_RESULT_MAX_COUNT) {
        idx = sl_ble_find_timeout_device();
        if (idx < 0) return;
    } else {
        idx = g_ble_scan_state.scan_count;
    }
    // Add/replace new device
    sl_ble_scan_info_t *info = &g_ble_scan_state.scan_results[idx];
    
    info->addr_type = adv_report->dev_addr_type;
    memcpy(info->addr, adv_report->dev_addr, 6);
    info->rssi = adv_report->rssi;
    info->adv_type = adv_report->report_type;
    info->last_seen_tick = osKernelGetTickCount();
    
    // Extract device name from advertisement data
    memset(info->name, 0, sizeof(info->name));
    BT_LE_ADPacketExtract(info->name, adv_report->adv_data, adv_report->adv_data_len);
    if (g_ble_scan_state.scan_count < SL_BLE_SCAN_RESULT_MAX_COUNT) g_ble_scan_state.scan_count++;
}

// BLE advertisement report callback
static void sl_ble_adv_report_callback(rsi_ble_event_adv_report_t *adv_report)
{
    if (!g_ble_scan_state.is_scanning) {
        return;
    }

    osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
    
    // Add or update device (hardware filtering is already done)
    sl_ble_add_device(adv_report);
    
    // Call user callback if set
    if (g_ble_scan_state.config.callback) {
        int idx = sl_ble_find_device(adv_report->dev_addr, adv_report->dev_addr_type);
        if (idx >= 0) {
            g_ble_scan_state.config.callback(&g_ble_scan_state.scan_results[idx]);
        }
    }
    
    osMutexRelease(g_ble_scan_state.mutex);
}

// Scan timeout thread
static void sl_ble_scan_timeout_thread(void *arg)
{
    uint32_t duration = (uint32_t)(uintptr_t)arg;
    
    osDelay(duration);
    
    osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
    if (g_ble_scan_state.is_scanning) {
        g_ble_scan_state.is_scanning = 0;
        rsi_ble_stop_scanning();
        
        // Release semaphore to unblock waiting thread
        if (g_ble_scan_state.scan_sem) {
            osSemaphoreRelease(g_ble_scan_state.scan_sem);
        }
    }
    g_ble_scan_state.scan_timeout_thread = NULL;
    osMutexRelease(g_ble_scan_state.mutex);
    
    osThreadExit();
}

uint8_t sl_ble_is_scanning(void)
{
    // If mutex is not initialized yet, no scan is running
    if (g_ble_scan_state.mutex == NULL) {
        return 0;
    }

    uint8_t scanning;

    osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
    scanning = g_ble_scan_state.is_scanning;
    osMutexRelease(g_ble_scan_state.mutex);

    return scanning;
}

int sl_ble_scan_start(sl_ble_scan_config_t *config)
{
    if (config == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    // Initialize mutex if needed
    if (g_ble_scan_state.mutex == NULL) {
        g_ble_scan_state.mutex = osMutexNew(NULL);
        if (g_ble_scan_state.mutex == NULL) {
            return AICAM_ERROR_NO_MEMORY;
        }
    }
    
    osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
    
    // Check if already scanning
    if (g_ble_scan_state.is_scanning) {
        osMutexRelease(g_ble_scan_state.mutex);
        return AICAM_ERROR_BUSY;
    }

    // Terminate timeout thread if exists
    if (g_ble_scan_state.scan_timeout_thread != NULL) {
        osThreadTerminate(g_ble_scan_state.scan_timeout_thread);
        g_ble_scan_state.scan_timeout_thread = NULL;
    }
    
    // Clear previous results
    g_ble_scan_state.scan_count = 0;
    memset(g_ble_scan_state.scan_results, 0, sizeof(g_ble_scan_state.scan_results));
    
    // Copy config (but not accept_list, it's only used during hardware setup)
    memcpy(&g_ble_scan_state.config, config, sizeof(sl_ble_scan_config_t));
    // Clear accept_list pointer in stored config since we don't need it after hardware setup
    g_ble_scan_state.config.accept_list = NULL;
    g_ble_scan_state.config.accept_num = 0;
    
    // Configure hardware accept list if provided (use original config, not stored copy)
    if (config->accept_num > 0 && config->accept_list != NULL) {
        // Clear accept list first
        int32_t ret = rsi_ble_clear_acceptlist();
        if (ret != 0) {
            LOG_DRV_ERROR("rsi_ble_clear_acceptlist failed: %d\n", ret);
            osMutexRelease(g_ble_scan_state.mutex);
            return AICAM_ERROR_HARDWARE;
        }
        
        // Add devices to accept list
        for (int i = 0; i < config->accept_num; i++) {
            sl_ble_addr_t *device = &config->accept_list[i];
            ret = rsi_ble_addto_acceptlist((const int8_t *)device->addr, device->addr_type);
            if (ret != 0) {
                LOG_DRV_ERROR("rsi_ble_addto_acceptlist failed: %d\n", ret);
                osMutexRelease(g_ble_scan_state.mutex);
                return AICAM_ERROR_HARDWARE;
            }
        }
    }
    
    // Prepare scan parameters
    rsi_ble_req_scan_t scan_params = {0};
    scan_params.status = RSI_BLE_START_SCAN;
    scan_params.scan_type = g_ble_scan_state.config.scan_type;
    scan_params.scan_int = g_ble_scan_state.config.scan_int;
    scan_params.scan_win = g_ble_scan_state.config.scan_win;
    scan_params.own_addr_type = LE_PUBLIC_ADDRESS;
    
    // Set filter type based on accept list (use original config)
    if (config->accept_num > 0 && config->accept_list != NULL) {
        scan_params.filter_type = SCAN_FILTER_TYPE_ONLY_ACCEPT_LIST;
    } else {
        scan_params.filter_type = SCAN_FILTER_TYPE_ALL;
    }
    
    // Register BLE callbacks if not already registered
    // Note: This should ideally be done once during initialization
    // For now, we register the callback each time
    rsi_ble_gap_register_callbacks(
        sl_ble_adv_report_callback,  // adv_report
        NULL,                         // conn_status
        NULL,                         // disconnect
        NULL,                         // le_ping_timeout
        NULL,                         // phy_update
        NULL,                         // data_length_update
        NULL,                         // enhance_conn_status
        NULL,                         // directed_adv_report
        NULL,                         // conn_update_complete
        NULL                          // remote_conn_params_request
    );
    
    // Start scanning
    int32_t ret = rsi_ble_start_scanning_with_values(&scan_params);
    if (ret != 0) {
        LOG_DRV_ERROR("rsi_ble_start_scanning_with_values failed: %d\n", ret);
        osMutexRelease(g_ble_scan_state.mutex);
        return AICAM_ERROR_HARDWARE;
    }
    
    g_ble_scan_state.is_scanning = 1;
    g_ble_scan_state.scan_start_time = osKernelGetTickCount();
    
    // Create semaphore for blocking mode if needed
    if (g_ble_scan_state.config.callback == NULL && g_ble_scan_state.config.scan_duration > 0) {
        if (g_ble_scan_state.scan_sem == NULL) {
            g_ble_scan_state.scan_sem = osSemaphoreNew(1, 0, NULL);
            if (g_ble_scan_state.scan_sem == NULL) {
                osMutexRelease(g_ble_scan_state.mutex);
                return AICAM_ERROR_NO_MEMORY;
            }
        } else {
            // Reset semaphore if it exists (try to acquire without blocking)
            osSemaphoreAcquire(g_ble_scan_state.scan_sem, 0);
        }
        
        // Create timeout thread
        const osThreadAttr_t thread_attr = {
            .name = "ble_scan_timeout",
            .stack_size = 1024 * 4,
            .priority = osPriorityNormal
        };
        g_ble_scan_state.scan_timeout_thread = osThreadNew(
            sl_ble_scan_timeout_thread, 
            (void *)(uintptr_t)g_ble_scan_state.config.scan_duration, 
            &thread_attr
        );
        
        if (g_ble_scan_state.scan_timeout_thread == NULL) {
            osMutexRelease(g_ble_scan_state.mutex);
            return AICAM_ERROR_NO_MEMORY;
        }
    }
    
    osMutexRelease(g_ble_scan_state.mutex);
    
    // Note: Non-blocking mode - function returns immediately
    // If blocking mode is needed, caller should wait using semaphore or callback
    
    return AICAM_OK;  // AICAM_OK is 0
}

int sl_ble_scan_stop(void)
{
    osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
    
    if (!g_ble_scan_state.is_scanning) {
        osMutexRelease(g_ble_scan_state.mutex);
        return AICAM_OK; // Already stopped, AICAM_OK is 0
    }
    
    g_ble_scan_state.is_scanning = 0;
    
    // Stop scanning
    int32_t ret = rsi_ble_stop_scanning();
    
    // Terminate timeout thread if exists
    if (g_ble_scan_state.scan_timeout_thread != NULL) {
        osThreadTerminate(g_ble_scan_state.scan_timeout_thread);
        g_ble_scan_state.scan_timeout_thread = NULL;
    }
    
    // Release semaphore if waiting
    if (g_ble_scan_state.scan_sem) {
        osSemaphoreRelease(g_ble_scan_state.scan_sem);
    }
    
    osMutexRelease(g_ble_scan_state.mutex);
    
    return (ret == 0) ? AICAM_OK : AICAM_ERROR_HARDWARE;
}

sl_ble_scan_result_t *sl_ble_scan_get_result(void)
{
    static sl_ble_scan_result_t result = {0};
    
    osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
    
    result.scan_count = g_ble_scan_state.scan_count;
    result.scan_info = g_ble_scan_state.scan_results;
    
    osMutexRelease(g_ble_scan_state.mutex);
    
    return &result;
}

void sl_ble_printf_scan_result(sl_ble_scan_result_t *scan_result)
{
    if (scan_result == NULL) {
        return;
    }

    uint32_t timeout_ms = SL_BLE_SCAN_REPLACE_TIMEOUT_MS;

    printf("BLE Scan Results (%d devices):\n", scan_result->scan_count);
    printf("--------------------------------------------------------------------------------\n");
    printf("Idx  Address               Type    RSSI(dBm)  Adv   Timeout(ms)  Name\n");
    printf("--------------------------------------------------------------------------------\n");

    for (int i = 0; i < scan_result->scan_count; i++) {
        sl_ble_scan_info_t *info = &scan_result->scan_info[i];

        // Index
        printf("%-4d", i + 1);

        // Address
        printf("%02X:%02X:%02X:%02X:%02X:%02X  ",
               info->addr[5], info->addr[4], info->addr[3],
               info->addr[2], info->addr[1], info->addr[0]);

        // Type (fixed width)
        printf("%-7s", info->addr_type == 0 ? "Public" : "Random");

        // RSSI, right-aligned in 4 chars
        printf("%7d     ", info->rssi);

        // Adv type
        printf("0x%02X  ", info->adv_type);

        // Timeout (time since last seen)
        timeout_ms = SL_RSI_BLE_TICK_DIFF_MS(info->last_seen_tick, osKernelGetTickCount());
        if (timeout_ms > SL_BLE_SCAN_REPLACE_TIMEOUT_MS) {
            printf("   > %dS   ", SL_BLE_SCAN_REPLACE_TIMEOUT_MS / 1000);
        } else {
            printf("%6.2fS  ", (float)timeout_ms / 1000.0f);
        }

        // Name
        if (info->name[0] != '\0') {
            printf("%s", info->name);
        } else {
            printf("(N/A)");
        }

        printf("\n");
    }

    printf("--------------------------------------------------------------------------------\n");
}

// Test accept list management
#define BLE_TEST_ACCEPT_LIST_MAX 10
static sl_ble_addr_t g_test_accept_list[BLE_TEST_ACCEPT_LIST_MAX];
static uint8_t g_test_accept_list_count = 0;

// Helper function to parse MAC address
static int parse_mac_address(const char *str, uint8_t *addr, uint8_t *addr_type)
{
    if (str == NULL || addr == NULL) {
        return -1;
    }
    
    int values[6];
    int count = sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X", 
                       &values[0], &values[1], &values[2], 
                       &values[3], &values[4], &values[5]);
    if (count != 6) {
        return -1;
    }
    for (int i = 0; i < 6; i++) {
        if (values[i] < 0 || values[i] > 255) {
            return -1;
        }
        addr[i] = (uint8_t)values[i];
    }
    // Default to public address type if not specified
    if (addr_type != NULL) {
        *addr_type = LE_PUBLIC_ADDRESS;
    }
    return 0;
}

// Unified test command handler
static int ble_test_cmd(int argc, char *argv[])
{
    uint8_t mac_addr[6] = { 0 };
    int ret = 0;

    if (argc < 2) {
        printf("Usage: ble <command> [args...]\n");
        printf("Commands:\n");
        printf("  mac - Show MAC address\n");
        printf("  scan_start [scan_type] [duration_sec] [rssi_threshold] [scan_int] [scan_win] - Start scan (non-blocking)\n");
        printf("  scan_stop - Stop scan\n");
        printf("  scan_result - Show scan results\n");
        printf("  scan_status - Show current scan status\n");
        printf("  scan_accept_add <mac> [addr_type] - Add device to accept list (mac: XX:XX:XX:XX:XX:XX)\n");
        printf("  scan_accept_del <mac> - Remove device from accept list\n");
        printf("  scan_accept_list - Show accept list\n");
        printf("  scan_accept_clear - Clear accept list\n");
        printf("  connect <scan_index> [timeout_ms] - Connect to device from scan results\n");
        printf("  disconnect <conn_handle> - Disconnect from device\n");
        printf("  conn_list - List all connections\n");
        printf("  conn_info <conn_handle> - Show connection information\n");
        printf("  discover_services <conn_handle> [uuid] - Discover services (uuid: 16-bit hex, e.g., 0x1800)\n");
        printf("  services <conn_handle> - List discovered services\n");
        printf("  discover_chars <conn_handle> <service_index> [uuid] - Discover characteristics\n");
        printf("  read_char <conn_handle> <char_handle> - Read characteristic value\n");
        printf("  write_char <conn_handle> <char_handle> <data> [no_resp] - Write characteristic (data: hex string)\n");
        printf("  enable_notify <conn_handle> <char_handle> - Enable notifications/indications\n");
        return -1;
    }
    
    const char *cmd = argv[1];
    
    // Check scan state for commands that require scan to be stopped
    if (strcmp(cmd, "scan_accept_add") == 0 || strcmp(cmd, "scan_accept_del") == 0 || 
        strcmp(cmd, "scan_accept_clear") == 0) {
        if (g_ble_scan_state.mutex != NULL) {
            osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
            if (g_ble_scan_state.is_scanning) {
                osMutexRelease(g_ble_scan_state.mutex);
                printf("Error: Cannot modify accept list while scanning. Please stop scan first.\n");
                return -1;
            }
            osMutexRelease(g_ble_scan_state.mutex);
        }
    }
    
    if (strcmp(cmd, "mac") == 0) {
        ret = rsi_bt_get_local_device_address(mac_addr);
        if (ret != RSI_SUCCESS) {
            printf("Failed to get MAC address: %d\n", ret);
        } else {
            printf("MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac_addr[5], mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]);
        }
        return ret;
    } else if (strcmp(cmd, "scan_start") == 0) {
        // Check if already scanning
        if (sl_ble_is_scanning()) {
            printf("Error: Scan is already in progress. Please stop it first.\n");
            return -1;
        }
        
        sl_ble_scan_config_t config = {0};
        
        // Default scan parameters
        config.scan_type = 0x01;  // SCAN_TYPE_ACTIVE
        config.scan_int = 0x0100;  // 256 * 0.625ms = 160ms
        config.scan_win = 0x0050;  // 80 * 0.625ms = 50ms
        config.scan_duration = 0;  // 0 means infinite (non-blocking)
        config.rssi_threshold = -127;  // No RSSI filtering by default
        config.accept_num = g_test_accept_list_count;
        config.accept_list = (g_test_accept_list_count > 0) ? g_test_accept_list : NULL;
        config.callback = NULL;  // No callback
        
        // Parse arguments: scan_type [duration_sec] [rssi_threshold] [scan_int] [scan_win]
        if (argc > 2) {
            config.scan_type = (uint8_t)atoi(argv[2]);
        }
        if (argc > 3) {
            config.scan_duration = (uint32_t)atoi(argv[3]) * 1000;  // Convert seconds to milliseconds
        }
        if (argc > 4) {
            config.rssi_threshold = (int8_t)atoi(argv[4]);
        }
        if (argc > 5) {
            config.scan_int = (uint16_t)strtoul(argv[5], NULL, 0);
        }
        if (argc > 6) {
            config.scan_win = (uint16_t)strtoul(argv[6], NULL, 0);
        }
        
        printf("Starting BLE scan (non-blocking):\n");
        printf("  Type: %s\n", config.scan_type == 0x01 ? "active" : "passive");
        if (config.scan_duration > 0) {
            printf("  Duration: %lu ms\n", (unsigned long)config.scan_duration);
        } else {
            printf("  Duration: infinite\n");
        }
        printf("  Interval: 0x%04X (%.1f ms)\n", config.scan_int, config.scan_int * 0.625f);
        printf("  Window: 0x%04X (%.1f ms)\n", config.scan_win, config.scan_win * 0.625f);
        if (config.rssi_threshold > -127) {
            printf("  RSSI threshold: %d dBm\n", config.rssi_threshold);
        } else {
            printf("  RSSI threshold: disabled\n");
        }
        printf("  Accept list: %d devices\n", config.accept_num);
        
        int ret = sl_ble_scan_start(&config);
        if (ret != AICAM_OK) {
            printf("BLE scan start failed: %s (%d)\n", aicam_error_to_string((aicam_result_t)ret), ret);
            return -1;
        }
        
        printf("BLE scan started successfully.\n");
        printf("Use 'ble scan_stop' to stop scanning or 'ble scan_result' to view results.\n");
        
        return 0;
    }
    else if (strcmp(cmd, "scan_stop") == 0) {
        // Check scan state
        if (!sl_ble_is_scanning()) {
            printf("Scan is not in progress.\n");
            return 0;
        }

        int ret = sl_ble_scan_stop();
        if (ret == AICAM_OK) {
            printf("BLE scan stopped.\n");
        } else {
            printf("BLE scan stop failed: %s (%d)\n", aicam_error_to_string((aicam_result_t)ret), ret);
        }
        return (ret == AICAM_OK) ? 0 : -1;
    }
    else if (strcmp(cmd, "scan_status") == 0) {
        uint8_t scanning = sl_ble_is_scanning();
        uint32_t elapsed_ms = 0;
        uint8_t count = 0;

        if (g_ble_scan_state.mutex != NULL) {
            osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
            if (scanning) {
                elapsed_ms = osKernelGetTickCount() - g_ble_scan_state.scan_start_time;
            }
            count = g_ble_scan_state.scan_count;
            osMutexRelease(g_ble_scan_state.mutex);
        }

        printf("BLE scan status: %s\n", scanning ? "running" : "stopped");
        if (scanning) {
            printf("  Elapsed: %lu ms\n", (unsigned long)elapsed_ms);
        }
        printf("  Devices found: %d\n", count);
        return 0;
    }
    else if (strcmp(cmd, "scan_result") == 0) {
        sl_ble_scan_result_t *result = sl_ble_scan_get_result();
        if (result) {
            sl_ble_printf_scan_result(result);
        } else {
            printf("No scan results available.\n");
        }
        return 0;
    }
    else if (strcmp(cmd, "scan_accept_add") == 0) {
        if (argc < 3) {
            printf("Usage: ble scan_accept_add <mac> [addr_type]\n");
            printf("  mac: MAC address in format XX:XX:XX:XX:XX:XX\n");
            printf("  addr_type: 0=Public, 1=Random (default: 0)\n");
            return -1;
        }
        
        if (g_test_accept_list_count >= BLE_TEST_ACCEPT_LIST_MAX) {
            printf("Accept list is full (max %d devices)\n", BLE_TEST_ACCEPT_LIST_MAX);
            return -1;
        }
        
        sl_ble_addr_t *device = &g_test_accept_list[g_test_accept_list_count];
        uint8_t addr_type = LE_PUBLIC_ADDRESS;
        
        if (parse_mac_address(argv[2], device->addr, &addr_type) != 0) {
            printf("Invalid MAC address format. Use XX:XX:XX:XX:XX:XX\n");
            return -1;
        }

        // Convert from human-readable big-endian to hardware order (little-endian)
        uint8_t hw_addr[6];
        for (int i = 0; i < 6; i++) {
            hw_addr[i] = device->addr[5 - i];
        }
        memcpy(device->addr, hw_addr, sizeof(hw_addr));
        
        if (argc > 3) {
            addr_type = (uint8_t)atoi(argv[3]);
        }
        device->addr_type = addr_type;
        
        g_test_accept_list_count++;
        printf("Added device to accept list: %02X:%02X:%02X:%02X:%02X:%02X (type: %d)\n",
               device->addr[5], device->addr[4], device->addr[3],
               device->addr[2], device->addr[1], device->addr[0],
               device->addr_type);
        return 0;
    }
    else if (strcmp(cmd, "scan_accept_del") == 0) {
        if (argc < 3) {
            printf("Usage: ble scan_accept_del <mac>\n");
            return -1;
        }
        
        uint8_t target_addr[6];
        if (parse_mac_address(argv[2], target_addr, NULL) != 0) {
            printf("Invalid MAC address format. Use XX:XX:XX:XX:XX:XX\n");
            return -1;
        }

        // Convert from human-readable big-endian to hardware order (little-endian)
        uint8_t hw_addr[6];
        for (int i = 0; i < 6; i++) {
            hw_addr[i] = target_addr[5 - i];
        }
        memcpy(target_addr, hw_addr, sizeof(hw_addr));
        
        int found = -1;
        for (int i = 0; i < g_test_accept_list_count; i++) {
            if (memcmp(g_test_accept_list[i].addr, target_addr, 6) == 0) {
                found = i;
                break;
            }
        }
        
        if (found < 0) {
            printf("Device not found in accept list\n");
            return -1;
        }
        
        // Remove by shifting remaining elements
        for (int i = found; i < g_test_accept_list_count - 1; i++) {
            g_test_accept_list[i] = g_test_accept_list[i + 1];
        }
        g_test_accept_list_count--;
        
        printf("Removed device from accept list: %02X:%02X:%02X:%02X:%02X:%02X\n",
               target_addr[5], target_addr[4], target_addr[3],
               target_addr[2], target_addr[1], target_addr[0]);
        return 0;
    }
    else if (strcmp(cmd, "scan_accept_list") == 0) {
        printf("Accept list (%d/%d devices):\n", g_test_accept_list_count, BLE_TEST_ACCEPT_LIST_MAX);
        if (g_test_accept_list_count == 0) {
            printf("  (empty)\n");
        } else {
            for (int i = 0; i < g_test_accept_list_count; i++) {
                sl_ble_addr_t *device = &g_test_accept_list[i];
                printf("  [%d] %02X:%02X:%02X:%02X:%02X:%02X (type: %s)\n",
                       i + 1,
                       device->addr[5], device->addr[4], device->addr[3],
                       device->addr[2], device->addr[1], device->addr[0],
                       device->addr_type == LE_PUBLIC_ADDRESS ? "Public" : "Random");
            }
        }
        return 0;
    }
    else if (strcmp(cmd, "scan_accept_clear") == 0) {
        g_test_accept_list_count = 0;
        memset(g_test_accept_list, 0, sizeof(g_test_accept_list));
        printf("Accept list cleared.\n");
        return 0;
    }
    else if (strcmp(cmd, "connect") == 0) {
        if (argc < 3) {
            printf("Usage: ble connect <scan_index> [timeout_ms]\n");
            printf("  scan_index: Index from scan results (use 'ble scan_result' to see)\n");
            printf("  timeout_ms: Connection timeout in milliseconds (default: 30000)\n");
            return -1;
        }
        
        int scan_index = atoi(argv[2]);
        sl_ble_scan_result_t *scan_result = sl_ble_scan_get_result();
        
        if (scan_result == NULL || scan_index < 0 || scan_index >= scan_result->scan_count) {
            printf("Error: Invalid scan index. Use 'ble scan_result' to see available devices.\n");
            return -1;
        }
        
        sl_ble_scan_info_t *scan_info = &scan_result->scan_info[scan_index];
        sl_ble_conn_config_t config = {0};
        uint32_t timeout_ms = (argc > 3) ? (uint32_t)atoi(argv[3]) : SL_BLE_CONNECTION_TIMEOUT_MS;
        config.timeout_ms = timeout_ms;
        config.conn_params = NULL; // Use default connection parameters
        
        printf("Connecting to device [%d]: %02X:%02X:%02X:%02X:%02X:%02X (type: %s, RSSI: %d)\n",
               scan_index,
               scan_info->addr[5], scan_info->addr[4], scan_info->addr[3],
               scan_info->addr[2], scan_info->addr[1], scan_info->addr[0],
               scan_info->addr_type == LE_PUBLIC_ADDRESS ? "Public" : "Random",
               scan_info->rssi);
        
        sl_ble_conn_handle_t conn_handle;
        int ret = sl_ble_connect(scan_info, &config, &conn_handle);
        
        if (ret == AICAM_OK) {
            printf("Connected successfully! Connection handle: %d\n", conn_handle);
            return 0;
        } else {
            printf("Connection failed: %s (%d)\n", aicam_error_to_string((aicam_result_t)ret), ret);
            return -1;
        }
    }
    else if (strcmp(cmd, "disconnect") == 0) {
        if (argc < 3) {
            printf("Usage: ble disconnect <conn_handle>\n");
            return -1;
        }
        
        sl_ble_conn_handle_t conn_handle = (sl_ble_conn_handle_t)atoi(argv[2]);
        int ret = sl_ble_disconnect(conn_handle);
        
        if (ret == AICAM_OK) {
            printf("Disconnected successfully from handle %d\n", conn_handle);
            return 0;
        } else {
            printf("Disconnect failed: %s (%d)\n", aicam_error_to_string((aicam_result_t)ret), ret);
            return -1;
        }
    }
    else if (strcmp(cmd, "conn_list") == 0) {
        sl_ble_conn_init();
        osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
        
        printf("BLE Connections:\n");
        printf("--------------------------------------------------------------------------------\n");
        printf("Handle  Address               Type    Status      Services  Connected Time\n");
        printf("--------------------------------------------------------------------------------\n");
        
        int count = 0;
        for (int i = 0; i < SL_BLE_MAX_CONNECTIONS; i++) {
            if (g_ble_conn_state.devices[i].handle != SL_BLE_INVALID_CONN_HANDLE &&
                g_ble_conn_state.devices[i].status == SL_BLE_CONN_STATUS_CONNECTED) {
                sl_ble_connected_device_t *dev = &g_ble_conn_state.devices[i];
                uint32_t connected_time = osKernelGetTickCount() - dev->connect_time;
                
                printf("%-6d  ", dev->handle);
                printf("%02X:%02X:%02X:%02X:%02X:%02X  ",
                       dev->addr.addr[5], dev->addr.addr[4], dev->addr.addr[3],
                       dev->addr.addr[2], dev->addr.addr[1], dev->addr.addr[0]);
                printf("%-7s  ", dev->addr.addr_type == LE_PUBLIC_ADDRESS ? "Public" : "Random");
                printf("%-10s  ", "Connected");
                printf("%-9d  ", dev->service_count);
                printf("%lu ms\n", (unsigned long)connected_time);
                count++;
            }
        }
        
        if (count == 0) {
            printf("  (no connections)\n");
        }
        
        printf("--------------------------------------------------------------------------------\n");
        printf("Total: %d connection(s)\n", count);
        
        osMutexRelease(g_ble_conn_state.mutex);
        return 0;
    }
    else if (strcmp(cmd, "conn_info") == 0) {
        if (argc < 3) {
            printf("Usage: ble conn_info <conn_handle>\n");
            return -1;
        }
        
        sl_ble_conn_handle_t conn_handle = (sl_ble_conn_handle_t)atoi(argv[2]);
        sl_ble_connected_device_t device;
        
        int ret = sl_ble_get_connected_device(conn_handle, &device);
        if (ret != AICAM_OK) {
            printf("Error: Invalid connection handle or device not connected: %s (%d)\n",
                   aicam_error_to_string((aicam_result_t)ret), ret);
            return -1;
        }
        
        printf("Connection Information (Handle: %d):\n", conn_handle);
        printf("--------------------------------------------------------------------------------\n");
        printf("Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
               device.addr.addr[5], device.addr.addr[4], device.addr.addr[3],
               device.addr.addr[2], device.addr.addr[1], device.addr.addr[0]);
        printf("Address Type: %s\n", device.addr.addr_type == LE_PUBLIC_ADDRESS ? "Public" : "Random");
        printf("Status: %s\n", device.status == SL_BLE_CONN_STATUS_CONNECTED ? "Connected" : "Disconnected");
        printf("MTU: %d\n", device.mtu);
        printf("Connection Interval: %d (%.2f ms)\n", device.conn_params.conn_interval_min,
               device.conn_params.conn_interval_min * 1.25f);
        printf("Connection Latency: %d\n", device.conn_params.conn_latency);
        printf("Supervision Timeout: %d (%.2f ms)\n", device.conn_params.supervision_timeout,
               device.conn_params.supervision_timeout * 10.0f);
        printf("Services Discovered: %d\n", device.service_count);
        printf("Connected Time: %lu ms\n", (unsigned long)(osKernelGetTickCount() - device.connect_time));
        printf("--------------------------------------------------------------------------------\n");
        
        return 0;
    }
    else if (strcmp(cmd, "discover_services") == 0) {
        if (argc < 3) {
            printf("Usage: ble discover_services <conn_handle> [uuid]\n");
            printf("  uuid: Optional 16-bit UUID filter (hex, e.g., 0x1800)\n");
            return -1;
        }
        
        sl_ble_conn_handle_t conn_handle = (sl_ble_conn_handle_t)atoi(argv[2]);
        sl_ble_service_t services[SL_BLE_MAX_SERVICES_PER_DEVICE];
        uint8_t service_count = SL_BLE_MAX_SERVICES_PER_DEVICE;
        
        sl_ble_service_filter_t filter = {0};
        uuid_t uuid_filter = {0};
        
        if (argc > 3) {
            uint16_t uuid_val = (uint16_t)strtoul(argv[3], NULL, 0);
            uuid_filter.size = 16;
            uuid_filter.val.val16 = uuid_val;
            filter.uuid_filter = &uuid_filter;
        }
        
        printf("Discovering services on connection %d...\n", conn_handle);
        int ret = sl_ble_discover_services(conn_handle, (argc > 3) ? &filter : NULL, services, &service_count);
        
        if (ret == AICAM_OK) {
            printf("Found %d service(s):\n", service_count);
            for (int i = 0; i < service_count; i++) {
                printf("  [%d] Handle: 0x%04X-0x%04X, UUID: ", i, services[i].start_handle, services[i].end_handle);
                if (services[i].uuid.size == 16) {
                    printf("0x%04X\n", services[i].uuid.val.val16);
                } else {
                    printf("(128-bit UUID)\n");
                }
            }
            return 0;
        } else {
            printf("Service discovery failed: %s (%d)\n", aicam_error_to_string((aicam_result_t)ret), ret);
            return -1;
        }
    }
    else if (strcmp(cmd, "services") == 0) {
        if (argc < 3) {
            printf("Usage: ble services <conn_handle>\n");
            return -1;
        }
        
        sl_ble_conn_handle_t conn_handle = (sl_ble_conn_handle_t)atoi(argv[2]);
        sl_ble_connected_device_t device;
        
        int ret = sl_ble_get_connected_device(conn_handle, &device);
        if (ret != AICAM_OK) {
            printf("Error: Invalid connection handle: %s (%d)\n",
                   aicam_error_to_string((aicam_result_t)ret), ret);
            return -1;
        }
        
        printf("Services on connection %d:\n", conn_handle);
        printf("--------------------------------------------------------------------------------\n");
        printf("Index  Start Handle  End Handle  UUID        Characteristics\n");
        printf("--------------------------------------------------------------------------------\n");
        
        for (int i = 0; i < device.service_count; i++) {
            printf("%-5d  0x%04X        0x%04X      ", i, device.services[i].start_handle, device.services[i].end_handle);
            if (device.services[i].uuid.size == 16) {
                printf("0x%04X      ", device.services[i].uuid.val.val16);
            } else {
                printf("(128-bit)   ");
            }
            printf("%d\n", device.services[i].char_count);
        }
        
        if (device.service_count == 0) {
            printf("  (no services discovered)\n");
        }
        
        printf("--------------------------------------------------------------------------------\n");
        return 0;
    }
    else if (strcmp(cmd, "discover_chars") == 0) {
        if (argc < 4) {
            printf("Usage: ble discover_chars <conn_handle> <service_index> [uuid]\n");
            printf("  service_index: Index from 'ble services' command\n");
            printf("  uuid: Optional 16-bit UUID filter (hex, e.g., 0x2A00)\n");
            return -1;
        }
        
        sl_ble_conn_handle_t conn_handle = (sl_ble_conn_handle_t)atoi(argv[2]);
        int service_index = atoi(argv[3]);
        
        sl_ble_connected_device_t device;
        int ret = sl_ble_get_connected_device(conn_handle, &device);
        if (ret != AICAM_OK || service_index < 0 || service_index >= device.service_count) {
            printf("Error: Invalid connection handle or service index\n");
            return -1;
        }
        
        sl_ble_service_t *service = &device.services[service_index];
        sl_ble_characteristic_t characteristics[SL_BLE_MAX_CHARACTERISTICS_PER_SERVICE];
        uint8_t char_count = SL_BLE_MAX_CHARACTERISTICS_PER_SERVICE;
        
        sl_ble_char_filter_t filter = {0};
        uuid_t uuid_filter = {0};
        
        if (argc > 4) {
            uint16_t uuid_val = (uint16_t)strtoul(argv[4], NULL, 0);
            uuid_filter.size = 16;
            uuid_filter.val.val16 = uuid_val;
            filter.uuid_filter = &uuid_filter;
        }
        
        printf("Discovering characteristics for service [%d] on connection %d...\n", service_index, conn_handle);
        ret = sl_ble_discover_characteristics(conn_handle, service, (argc > 4) ? &filter : NULL, characteristics, &char_count);
        
        if (ret == AICAM_OK) {
            printf("Found %d characteristic(s):\n", char_count);
            for (int i = 0; i < char_count; i++) {
                printf("  [%d] Handle: 0x%04X, Decl: 0x%04X, CCCD: 0x%04X, UUID: ", 
                       i, characteristics[i].handle, characteristics[i].decl_handle, characteristics[i].cccd_handle);
                if (characteristics[i].uuid.size == 16) {
                    printf("0x%04X, Props: ", characteristics[i].uuid.val.val16);
                } else {
                    printf("(128-bit), Props: ");
                }
                if (characteristics[i].properties & SL_BLE_CHAR_PROP_READ) printf("R");
                if (characteristics[i].properties & SL_BLE_CHAR_PROP_WRITE) printf("W");
                if (characteristics[i].properties & SL_BLE_CHAR_PROP_WRITE_NO_RESP) printf("w");
                if (characteristics[i].properties & SL_BLE_CHAR_PROP_NOTIFY) printf("N");
                if (characteristics[i].properties & SL_BLE_CHAR_PROP_INDICATE) printf("I");
                printf("\n");
            }
            return 0;
        } else {
            printf("Characteristic discovery failed: %s (%d)\n", aicam_error_to_string((aicam_result_t)ret), ret);
            return -1;
        }
    }
    else if (strcmp(cmd, "read_char") == 0) {
        if (argc < 4) {
            printf("Usage: ble read_char <conn_handle> <char_handle>\n");
            printf("  char_handle: Characteristic value handle (hex, e.g., 0x0012)\n");
            return -1;
        }
        
        sl_ble_conn_handle_t conn_handle = (sl_ble_conn_handle_t)atoi(argv[2]);
        uint16_t char_handle = (uint16_t)strtoul(argv[3], NULL, 0);
        
        sl_ble_characteristic_t char_info = {0};
        char_info.handle = char_handle;
        
        uint8_t data[RSI_DEV_ATT_LEN];
        uint16_t data_len = sizeof(data);
        
        printf("Reading characteristic 0x%04X on connection %d...\n", char_handle, conn_handle);
        int ret = sl_ble_read_characteristic(conn_handle, &char_info, data, &data_len);
        
        if (ret == AICAM_OK) {
            printf("Read %d byte(s): ", data_len);
            for (int i = 0; i < data_len; i++) {
                printf("%02X ", data[i]);
            }
            printf("\n");
            if (data_len > 0 && data_len < 100) {
                printf("ASCII: ");
                for (int i = 0; i < data_len; i++) {
                    printf("%c", (data[i] >= 32 && data[i] < 127) ? data[i] : '.');
                }
                printf("\n");
            }
            return 0;
        } else {
            printf("Read failed: %s (%d)\n", aicam_error_to_string((aicam_result_t)ret), ret);
            return -1;
        }
    }
    else if (strcmp(cmd, "write_char") == 0) {
        if (argc < 5) {
            printf("Usage: ble write_char <conn_handle> <char_handle> <data> [no_resp]\n");
            printf("  char_handle: Characteristic value handle (hex, e.g., 0x0012)\n");
            printf("  data: Hex string (e.g., 01020304)\n");
            printf("  no_resp: 1 to use write without response, 0 for write with response (default: 0)\n");
            return -1;
        }
        
        sl_ble_conn_handle_t conn_handle = (sl_ble_conn_handle_t)atoi(argv[2]);
        uint16_t char_handle = (uint16_t)strtoul(argv[3], NULL, 0);
        const char *data_str = argv[4];
        uint8_t write_without_response = (argc > 5) ? (uint8_t)atoi(argv[5]) : 0;
        
        // Parse hex string
        uint8_t data[RSI_DEV_ATT_LEN];
        int data_len = 0;
        for (int i = 0; data_str[i] != '\0' && data_len < RSI_DEV_ATT_LEN; i += 2) {
            if (data_str[i] == '\0') break;
            char hex_byte[3] = {data_str[i], (data_str[i+1] != '\0') ? data_str[i+1] : '0', '\0'};
            data[data_len++] = (uint8_t)strtoul(hex_byte, NULL, 16);
            if (data_str[i+1] == '\0') break;
        }
        
        if (data_len == 0) {
            printf("Error: Invalid data format. Use hex string (e.g., 01020304)\n");
            return -1;
        }
        
        sl_ble_characteristic_t char_info = {0};
        char_info.handle = char_handle;
        char_info.properties = write_without_response ? SL_BLE_CHAR_PROP_WRITE_NO_RESP : SL_BLE_CHAR_PROP_WRITE;
        
        printf("Writing %d byte(s) to characteristic 0x%04X on connection %d...\n", data_len, char_handle, conn_handle);
        printf("Data: ");
        for (int i = 0; i < data_len; i++) {
            printf("%02X ", data[i]);
        }
        printf("\n");
        
        int ret = sl_ble_write_characteristic(conn_handle, &char_info, data, data_len, write_without_response);
        
        if (ret == AICAM_OK) {
            printf("Write successful\n");
            return 0;
        } else {
            printf("Write failed: %s (%d)\n", aicam_error_to_string((aicam_result_t)ret), ret);
            return -1;
        }
    }
    else if (strcmp(cmd, "enable_notify") == 0) {
        if (argc < 4) {
            printf("Usage: ble enable_notify <conn_handle> <char_handle>\n");
            printf("  char_handle: Characteristic value handle (hex, e.g., 0x0012)\n");
            printf("  Note: Characteristic must support notify/indicate and CCCD handle must be discovered\n");
            return -1;
        }
        
        sl_ble_conn_handle_t conn_handle = (sl_ble_conn_handle_t)atoi(argv[2]);
        uint16_t char_handle = (uint16_t)strtoul(argv[3], NULL, 0);
        
        // We need to find the characteristic to get its CCCD handle
        // For simplicity, we'll create a minimal characteristic structure
        // In a real implementation, you'd look it up from discovered characteristics
        sl_ble_characteristic_t char_info = {0};
        char_info.handle = char_handle;
        char_info.properties = SL_BLE_CHAR_PROP_NOTIFY; // Assume notify for now
        // Note: CCCD handle should be discovered first using discover_chars
        
        printf("Enabling notifications for characteristic 0x%04X on connection %d...\n", char_handle, conn_handle);
        printf("Warning: CCCD handle must be discovered first. Use 'discover_chars' command.\n");
        
        int ret = sl_ble_enable_notify(conn_handle, &char_info, NULL);
        
        if (ret == AICAM_OK) {
            printf("Notifications enabled successfully\n");
            return 0;
        } else {
            printf("Enable notify failed: %s (%d)\n", aicam_error_to_string((aicam_result_t)ret), ret);
            printf("Make sure to discover characteristics first and that CCCD handle is available.\n");
            return -1;
        }
    }
    else {
        printf("Unknown command: %s\n", cmd);
        printf("Use 'ble' without arguments to see usage.\n");
        return -1;
    }
}

// Command registration table
static debug_cmd_reg_t ble_test_cmd_table[] = {
    {"ble", "BLE test commands: ble <command> [args...]", ble_test_cmd},
};

void sl_ble_test_commands_register(void)
{
    debug_cmdline_register(ble_test_cmd_table, 
                          sizeof(ble_test_cmd_table) / sizeof(ble_test_cmd_table[0]));
}

// ========== Connection Management Implementation ==========

// Initialize connection state mutex
static void sl_ble_conn_init(void)
{
    if (g_ble_conn_state.mutex == NULL) {
        g_ble_conn_state.mutex = osMutexNew(NULL);
        g_ble_conn_state.conn_sem = osSemaphoreNew(1, 0, NULL);
        g_ble_conn_state.discovery_sem = osSemaphoreNew(1, 0, NULL);
        g_ble_conn_state.read_sem = osSemaphoreNew(1, 0, NULL);
        g_ble_conn_state.write_sem = osSemaphoreNew(1, 0, NULL);
        memset(g_ble_conn_state.devices, 0, sizeof(g_ble_conn_state.devices));
        memset(g_ble_conn_state.used_handles, 0, sizeof(g_ble_conn_state.used_handles));
        g_ble_conn_state.handle_count = 0;
    }
}

// Find device by address
static sl_ble_connected_device_t *sl_ble_find_device_by_addr(uint8_t *addr, uint8_t addr_type)
{
    for (int i = 0; i < SL_BLE_MAX_CONNECTIONS; i++) {
        if (g_ble_conn_state.devices[i].handle != SL_BLE_INVALID_CONN_HANDLE &&
            // g_ble_conn_state.devices[i].addr.addr_type == addr_type &&
            memcmp(g_ble_conn_state.devices[i].addr.addr, addr, 6) == 0) {
            return &g_ble_conn_state.devices[i];
        }
    }
    return NULL;
}

// Find device by handle
static sl_ble_connected_device_t *sl_ble_find_device_by_handle(sl_ble_conn_handle_t handle)
{
    if (handle == SL_BLE_INVALID_CONN_HANDLE) {
        return NULL;
    }
    for (int i = 0; i < SL_BLE_MAX_CONNECTIONS; i++) {
        if (g_ble_conn_state.devices[i].handle == handle) {
            return &g_ble_conn_state.devices[i];
        }
    }
    return NULL;
}

// Allocate connection handle
static sl_ble_conn_handle_t sl_ble_alloc_conn_handle(void)
{
    for (uint8_t i = 0; i < SL_BLE_MAX_CONNECTIONS; i++) {
        if (g_ble_conn_state.used_handles[i] == 0) {
            g_ble_conn_state.used_handles[i] = 1;
            g_ble_conn_state.handle_count++;
            return i;
        }
    }
    return SL_BLE_INVALID_CONN_HANDLE;
}

// Free connection handle
static void sl_ble_free_conn_handle(sl_ble_conn_handle_t handle)
{
    if (handle < SL_BLE_MAX_CONNECTIONS) {
        g_ble_conn_state.used_handles[handle] = 0;
        if (g_ble_conn_state.handle_count > 0) {
            g_ble_conn_state.handle_count--;
        }
    }
}

// UUID comparison helper
static int sl_ble_uuid_compare(const uuid_t *uuid1, const uuid_t *uuid2)
{
    if (uuid1->size != uuid2->size) {
        return 1;
    }
    if (uuid1->size == 16) {
        return memcmp(&uuid1->val.val16, &uuid2->val.val16, 2);
    } else if (uuid1->size == 32) {
        return memcmp(&uuid1->val.val32, &uuid2->val.val32, 4);
    } else if (uuid1->size == 128) {
        return memcmp(&uuid1->val.val128, &uuid2->val.val128, 16);
    }
    return 1;
}

// GAP callbacks
static void sl_ble_conn_status_callback(rsi_ble_event_conn_status_t *conn_status)
{
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    sl_ble_connected_device_t *device = sl_ble_find_device_by_addr(conn_status->dev_addr, conn_status->dev_addr_type);
    
    if (device != NULL) {
        if (conn_status->status == 0) {
            device->status = SL_BLE_CONN_STATUS_CONNECTED;
            device->connect_time = osKernelGetTickCount();
            LOG_DRV_INFO("BLE device connected: %02X:%02X:%02X:%02X:%02X:%02X\n",
                        conn_status->dev_addr[5], conn_status->dev_addr[4], conn_status->dev_addr[3],
                        conn_status->dev_addr[2], conn_status->dev_addr[1], conn_status->dev_addr[0]);
        } else {
            device->status = SL_BLE_CONN_STATUS_DISCONNECTED;
            LOG_DRV_ERROR("BLE connection failed: status=0x%04X\n", conn_status->status);
        }
    }
    
    if (g_ble_conn_state.conn_sem) {
        osSemaphoreRelease(g_ble_conn_state.conn_sem);
    }
    
    osMutexRelease(g_ble_conn_state.mutex);
}

static void sl_ble_enhance_conn_status_callback(rsi_ble_event_enhance_conn_status_t *enhance_conn_status)
{
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    sl_ble_connected_device_t *device = sl_ble_find_device_by_addr(enhance_conn_status->dev_addr, enhance_conn_status->dev_addr_type);
    
    if (device != NULL) {
        if (enhance_conn_status->status == 0) {
            device->status = SL_BLE_CONN_STATUS_CONNECTED;
            device->connect_time = osKernelGetTickCount();
            device->conn_params.conn_interval_min = enhance_conn_status->conn_interval;
            device->conn_params.conn_interval_max = enhance_conn_status->conn_interval;
            device->conn_params.conn_latency = enhance_conn_status->conn_latency;
            device->conn_params.supervision_timeout = enhance_conn_status->supervision_timeout;
            LOG_DRV_INFO("BLE device connected (enhanced): %02X:%02X:%02X:%02X:%02X:%02X, interval=%d, latency=%d\n",
                        enhance_conn_status->dev_addr[5], enhance_conn_status->dev_addr[4], enhance_conn_status->dev_addr[3],
                        enhance_conn_status->dev_addr[2], enhance_conn_status->dev_addr[1], enhance_conn_status->dev_addr[0],
                        enhance_conn_status->conn_interval, enhance_conn_status->conn_latency);
        } else {
            device->status = SL_BLE_CONN_STATUS_DISCONNECTED;
            LOG_DRV_ERROR("BLE enhanced connection failed: status=0x%04X\n", enhance_conn_status->status);
        }
    }
    
    if (g_ble_conn_state.conn_sem) {
        osSemaphoreRelease(g_ble_conn_state.conn_sem);
    }
    
    osMutexRelease(g_ble_conn_state.mutex);
}

static void sl_ble_disconnect_callback(rsi_ble_event_disconnect_t *disconnect, uint16_t reason)
{
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    sl_ble_connected_device_t *device = sl_ble_find_device_by_addr(disconnect->dev_addr, disconnect->dev_type);
    
    if (device != NULL) {
        device->status = SL_BLE_CONN_STATUS_DISCONNECTED;
        sl_ble_free_conn_handle(device->handle);
        memset(device, 0, sizeof(sl_ble_connected_device_t));
        device->handle = SL_BLE_INVALID_CONN_HANDLE;
        LOG_DRV_INFO("BLE device disconnected: %02X:%02X:%02X:%02X:%02X:%02X, reason=0x%04X\n",
                    disconnect->dev_addr[5], disconnect->dev_addr[4], disconnect->dev_addr[3],
                    disconnect->dev_addr[2], disconnect->dev_addr[1], disconnect->dev_addr[0], reason);
    }
    
    if (g_ble_conn_state.conn_sem) {
        osSemaphoreRelease(g_ble_conn_state.conn_sem);
    }
    
    osMutexRelease(g_ble_conn_state.mutex);
}

static void sl_ble_conn_update_complete_callback(rsi_ble_event_conn_update_t *conn_update, uint16_t resp_status)
{
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    sl_ble_connected_device_t *device = sl_ble_find_device_by_addr(conn_update->dev_addr, 0);
    
    if (device != NULL && resp_status == 0) {
        device->conn_params.conn_interval_min = conn_update->conn_interval;
        device->conn_params.conn_interval_max = conn_update->conn_interval;
        device->conn_params.conn_latency = conn_update->conn_latency;
        device->conn_params.supervision_timeout = conn_update->timeout;
        LOG_DRV_INFO("BLE connection parameters updated\n");
    }
    
    osMutexRelease(g_ble_conn_state.mutex);
}

// GATT callbacks - Note: These callbacks receive responses that are already stored in the response structures
// passed to the API calls. We just need to signal completion and store status.
static void sl_ble_profiles_list_resp_callback(uint16_t resp_status, rsi_ble_resp_profiles_list_t *profiles)
{
    sl_ble_conn_init();
    g_ble_conn_state.pending_resp_status = resp_status;
    
    // Note: The profiles data is already in the structure passed to rsi_ble_get_profiles()
    // We just signal completion here
    
    if (g_ble_conn_state.discovery_sem) {
        osSemaphoreRelease(g_ble_conn_state.discovery_sem);
    }
}

static void sl_ble_profile_resp_callback(uint16_t resp_status, profile_descriptors_t *profile)
{
    sl_ble_conn_init();
    g_ble_conn_state.pending_resp_status = resp_status;
    
    // Note: The profile data is already in the structure passed to rsi_ble_get_profile()
    
    if (g_ble_conn_state.discovery_sem) {
        osSemaphoreRelease(g_ble_conn_state.discovery_sem);
    }
}

static void sl_ble_char_services_resp_callback(uint16_t resp_status, rsi_ble_resp_char_services_t *char_services)
{
    sl_ble_conn_init();
    g_ble_conn_state.pending_resp_status = resp_status;
    
    // Note: The char_services data is already in the structure passed to rsi_ble_get_char_services()
    
    if (g_ble_conn_state.discovery_sem) {
        osSemaphoreRelease(g_ble_conn_state.discovery_sem);
    }
}

static void sl_ble_att_desc_resp_callback(uint16_t resp_status, rsi_ble_resp_att_descs_t *att_descs)
{
    sl_ble_conn_init();
    g_ble_conn_state.pending_resp_status = resp_status;
    
    // Note: The att_descs data is already in the structure passed to rsi_ble_get_att_descriptors()
    
    if (g_ble_conn_state.discovery_sem) {
        osSemaphoreRelease(g_ble_conn_state.discovery_sem);
    }
}

static void sl_ble_read_resp_callback(uint16_t resp_status, uint16_t resp_id, rsi_ble_resp_att_value_t *att_val)
{
    sl_ble_conn_init();
    g_ble_conn_state.pending_resp_status = resp_status;
    g_ble_conn_state.pending_resp_id = resp_id;
    
    // Note: The att_val data is already in the structure passed to rsi_ble_get_att_value()
    // We store a copy for later retrieval
    if (resp_status == 0 && att_val != NULL) {
        memcpy(&g_ble_conn_state.pending_read_data, att_val, sizeof(rsi_ble_resp_att_value_t));
    } else {
        memset(&g_ble_conn_state.pending_read_data, 0, sizeof(rsi_ble_resp_att_value_t));
    }
    
    if (g_ble_conn_state.read_sem) {
        osSemaphoreRelease(g_ble_conn_state.read_sem);
    }
}

static void sl_ble_write_resp_callback(uint16_t resp_status, uint16_t resp_id)
{
    sl_ble_conn_init();
    g_ble_conn_state.pending_resp_status = resp_status;
    g_ble_conn_state.pending_resp_id = resp_id;
    
    if (g_ble_conn_state.write_sem) {
        osSemaphoreRelease(g_ble_conn_state.write_sem);
    }
}

static void sl_ble_gatt_write_event_callback(uint16_t event_id, rsi_ble_event_write_t *write_event)
{
    sl_ble_conn_init();
    
    if (event_id == RSI_BLE_NOTIFICATION_EVENT || event_id == RSI_BLE_INDICATION_EVENT) {
        sl_ble_connected_device_t *device = sl_ble_find_device_by_addr(write_event->dev_addr, 0);
        
        if (device != NULL) {
            uint16_t handle = (write_event->handle[1] << 8) | write_event->handle[0];
            
            // Note: We need to store characteristic handles for proper callback lookup
            // For now, we'll use a simple approach - call all notify callbacks
            // In a full implementation, we'd maintain a handle-to-callback mapping
            // and iterate through services to find the matching characteristic
            
            // Call notification callback if registered
            // This is a simplified version - full implementation would map handle to callback
            LOG_DRV_DEBUG("BLE notification received: handle=0x%04X, len=%d\n", handle, write_event->length);
        }
    }
}

// Register BLE callbacks (should be called during initialization)
void sl_ble_register_callbacks(void)
{
    sl_ble_conn_init();
    
    // Register GAP callbacks
    rsi_ble_gap_register_callbacks(
        sl_ble_adv_report_callback,              // adv_report
        sl_ble_conn_status_callback,            // conn_status
        sl_ble_disconnect_callback,             // disconnect
        NULL,                                    // le_ping_timeout
        NULL,                                    // phy_update
        NULL,                                    // data_length_update
        sl_ble_enhance_conn_status_callback,    // enhance_conn_status
        NULL,                                    // directed_adv_report
        sl_ble_conn_update_complete_callback,   // conn_update_complete
        NULL                                     // remote_conn_params_request
    );
    
    // Register GATT callbacks
    rsi_ble_gatt_register_callbacks(
        sl_ble_profiles_list_resp_callback,     // profiles_list_resp
        sl_ble_profile_resp_callback,            // profile_resp
        sl_ble_char_services_resp_callback,     // char_services_resp
        NULL,                                    // inc_services_resp
        sl_ble_att_desc_resp_callback,          // att_desc_resp
        sl_ble_read_resp_callback,               // read_resp
        sl_ble_write_resp_callback,             // write_resp
        sl_ble_gatt_write_event_callback,       // gatt_write_event
        NULL,                                    // gatt_prepare_write_event
        NULL,                                    // execute_write_event
        NULL,                                    // read_req_event
        NULL,                                    // mtu_event
        NULL,                                    // gatt_error_resp_event
        NULL,                                    // gatt_desc_val_event
        NULL,                                    // event_profiles_list
        NULL,                                    // event_profile_by_uuid
        NULL,                                    // event_read_by_char_services
        NULL,                                    // event_read_by_inc_services
        NULL,                                    // event_read_att_value
        NULL,                                    // event_read_resp
        NULL,                                    // event_write_resp
        NULL,                                    // event_indicate_confirmation
        NULL                                     // event_prepare_write_resp
    );
}

// Public API implementations
int sl_ble_connect(sl_ble_scan_info_t *scan_info, sl_ble_conn_config_t *config, sl_ble_conn_handle_t *conn_handle)
{
    if (scan_info == NULL || conn_handle == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    // Check if already connected
    sl_ble_connected_device_t *existing = sl_ble_find_device_by_addr(scan_info->addr, scan_info->addr_type);
    if (existing != NULL && existing->status == SL_BLE_CONN_STATUS_CONNECTED) {
        *conn_handle = existing->handle;
        osMutexRelease(g_ble_conn_state.mutex);
        return AICAM_OK;
    }
    
    // Allocate connection handle
    sl_ble_conn_handle_t handle = sl_ble_alloc_conn_handle();
    if (handle == SL_BLE_INVALID_CONN_HANDLE) {
        osMutexRelease(g_ble_conn_state.mutex);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Initialize device structure
    sl_ble_connected_device_t *device = &g_ble_conn_state.devices[handle];
    memset(device, 0, sizeof(sl_ble_connected_device_t));
    device->handle = handle;
    device->addr.addr_type = scan_info->addr_type;
    memcpy(device->addr.addr, scan_info->addr, 6);
    device->status = SL_BLE_CONN_STATUS_CONNECTING;
    
    // Set connection parameters
    if (config != NULL && config->conn_params != NULL) {
        memcpy(&device->conn_params, config->conn_params, sizeof(sl_ble_conn_params_t));
    } else {
        // Default connection parameters
        device->conn_params.conn_interval_min = 0x0018;  // 30ms
        device->conn_params.conn_interval_max = 0x0028;  // 50ms
        device->conn_params.conn_latency = 0;
        device->conn_params.supervision_timeout = 0x00C8; // 2 seconds
        device->conn_params.scan_interval = 0x0100;      // 160ms
        device->conn_params.scan_window = 0x0050;       // 50ms
    }
    
    uint32_t timeout_ms = (config != NULL && config->timeout_ms > 0) ? config->timeout_ms : SL_BLE_CONNECTION_TIMEOUT_MS;
    
    osMutexRelease(g_ble_conn_state.mutex);
    
    // Stop scanning if active (as per SDK documentation)
    if (sl_ble_is_scanning()) {
        osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
        
        // Terminate timeout thread if exists
        if (g_ble_scan_state.scan_timeout_thread != NULL) {
            osThreadTerminate(g_ble_scan_state.scan_timeout_thread);
            g_ble_scan_state.scan_timeout_thread = NULL;
        }
        
        // Release semaphore if waiting
        if (g_ble_scan_state.scan_sem) {
            osSemaphoreRelease(g_ble_scan_state.scan_sem);
        }
        
        g_ble_scan_state.is_scanning = 0;

        osMutexRelease(g_ble_scan_state.mutex);
    }
    
    // Register callbacks if not already registered
    sl_ble_register_callbacks();
    
    // Initiate connection
    int32_t ret;
    if (config != NULL && config->conn_params != NULL) {
        ret = rsi_ble_connect_with_params(
            scan_info->addr_type,
            (const int8_t *)scan_info->addr,
            device->conn_params.scan_interval,
            device->conn_params.scan_window,
            device->conn_params.conn_interval_max,
            device->conn_params.conn_interval_min,
            device->conn_params.conn_latency,
            device->conn_params.supervision_timeout
        );
    } else {
        ret = rsi_ble_connect(scan_info->addr_type, (const int8_t *)scan_info->addr);
    }
    
    if (ret != 0) {
        osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
        sl_ble_free_conn_handle(handle);
        memset(device, 0, sizeof(sl_ble_connected_device_t));
        device->handle = SL_BLE_INVALID_CONN_HANDLE;
        osMutexRelease(g_ble_conn_state.mutex);
        LOG_DRV_ERROR("BLE connection failed: %d\n", ret);
        return AICAM_ERROR_HARDWARE;
    }
    
    // Wait for connection event
    if (osSemaphoreAcquire(g_ble_conn_state.conn_sem, timeout_ms) != osOK) {
        osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
        if (device->status != SL_BLE_CONN_STATUS_CONNECTED) {
            sl_ble_free_conn_handle(handle);
            memset(device, 0, sizeof(sl_ble_connected_device_t));
            device->handle = SL_BLE_INVALID_CONN_HANDLE;
            osMutexRelease(g_ble_conn_state.mutex);
            return AICAM_ERROR_TIMEOUT;
        }
        osMutexRelease(g_ble_conn_state.mutex);
    }
    
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    if (device->status == SL_BLE_CONN_STATUS_CONNECTED) {
        *conn_handle = handle;
        osMutexRelease(g_ble_conn_state.mutex);
        return AICAM_OK;
    } else {
        sl_ble_free_conn_handle(handle);
        memset(device, 0, sizeof(sl_ble_connected_device_t));
        device->handle = SL_BLE_INVALID_CONN_HANDLE;
        osMutexRelease(g_ble_conn_state.mutex);
        LOG_DRV_ERROR("BLE connection status: %d\n", device->status);
        return AICAM_ERROR_HARDWARE;
    }
}

int sl_ble_disconnect(sl_ble_conn_handle_t conn_handle)
{
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    sl_ble_connected_device_t *device = sl_ble_find_device_by_handle(conn_handle);
    if (device == NULL || device->status != SL_BLE_CONN_STATUS_CONNECTED) {
        osMutexRelease(g_ble_conn_state.mutex);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    device->status = SL_BLE_CONN_STATUS_DISCONNECTING;
    uint8_t addr[6];
    memcpy(addr, device->addr.addr, 6);
    
    osMutexRelease(g_ble_conn_state.mutex);
    
    int32_t ret = rsi_ble_disconnect((const int8_t *)addr);
    
    if (ret == 0) {
        // Wait for disconnect event
        osSemaphoreAcquire(g_ble_conn_state.conn_sem, SL_BLE_CONNECTION_TIMEOUT_MS);
    }
    
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    sl_ble_free_conn_handle(conn_handle);
    memset(device, 0, sizeof(sl_ble_connected_device_t));
    device->handle = SL_BLE_INVALID_CONN_HANDLE;
    osMutexRelease(g_ble_conn_state.mutex);
    
    return (ret == 0) ? AICAM_OK : AICAM_ERROR_HARDWARE;
}

int sl_ble_discover_services(sl_ble_conn_handle_t conn_handle, sl_ble_service_filter_t *filter, 
                             sl_ble_service_t *services, uint8_t *service_count)
{
    if (services == NULL || service_count == NULL || *service_count == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    sl_ble_connected_device_t *device = sl_ble_find_device_by_handle(conn_handle);
    if (device == NULL || device->status != SL_BLE_CONN_STATUS_CONNECTED) {
        osMutexRelease(g_ble_conn_state.mutex);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    uint16_t start_handle = (filter != NULL && filter->start_handle > 0) ? filter->start_handle : 0x0001;
    uint16_t end_handle = (filter != NULL && filter->end_handle > 0) ? filter->end_handle : 0xFFFF;
    
    osMutexRelease(g_ble_conn_state.mutex);
    
    // Discover all profiles/services
    // Note: rsi_ble_get_profiles is non-blocking but fills the response structure via callback
    rsi_ble_resp_profiles_list_t profiles_list = {0};
    int32_t ret = rsi_ble_get_profiles(device->addr.addr, start_handle, end_handle, &profiles_list);
    
    if (ret != 0) {
        LOG_DRV_ERROR("Failed to get profiles: %d", ret);
        return AICAM_ERROR_HARDWARE;
    }
    
    // Wait for callback to complete
    if (osSemaphoreAcquire(g_ble_conn_state.discovery_sem, SL_BLE_DISCOVERY_TIMEOUT_MS) != osOK) {
        return AICAM_ERROR_TIMEOUT;
    }
    
    if (g_ble_conn_state.pending_resp_status != 0) {
        LOG_DRV_ERROR("Get profiles status: %d", g_ble_conn_state.pending_resp_status);
        return AICAM_ERROR_HARDWARE;
    }
    
    // Copy discovered services (profiles_list is filled by callback)
    uint8_t count = 0;
    uint8_t max_count = (*service_count < profiles_list.number_of_profiles) ? *service_count : profiles_list.number_of_profiles;
    
    for (int i = 0; i < max_count; i++) {
        profile_descriptors_t *profile = &profiles_list.profile_desc[i];
        
        // Apply UUID filter if specified
        if (filter != NULL && filter->uuid_filter != NULL) {
            if (sl_ble_uuid_compare(&profile->profile_uuid, filter->uuid_filter) != 0) {
                continue;
            }
        }
        
        services[count].uuid = profile->profile_uuid;
        services[count].start_handle = (profile->start_handle[1] << 8) | profile->start_handle[0];
        services[count].end_handle = (profile->end_handle[1] << 8) | profile->end_handle[0];
        services[count].char_count = 0;
        count++;
    }
    
    *service_count = count;
    
    // Update device service list
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    if (count > 0) {
        uint8_t copy_count = (count < SL_BLE_MAX_SERVICES_PER_DEVICE) ? count : SL_BLE_MAX_SERVICES_PER_DEVICE;
        memcpy(device->services, services, copy_count * sizeof(sl_ble_service_t));
        device->service_count = copy_count;
    }
    osMutexRelease(g_ble_conn_state.mutex);
    
    return AICAM_OK;
}

int sl_ble_discover_characteristics(sl_ble_conn_handle_t conn_handle, sl_ble_service_t *service,
                                     sl_ble_char_filter_t *filter, sl_ble_characteristic_t *characteristics,
                                     uint8_t *char_count)
{
    if (service == NULL || characteristics == NULL || char_count == NULL || *char_count == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    sl_ble_connected_device_t *device = sl_ble_find_device_by_handle(conn_handle);
    if (device == NULL || device->status != SL_BLE_CONN_STATUS_CONNECTED) {
        osMutexRelease(g_ble_conn_state.mutex);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    uint16_t start_handle = (filter != NULL && filter->service_start_handle > 0) ? 
                             filter->service_start_handle : service->start_handle;
    uint16_t end_handle = (filter != NULL && filter->service_end_handle > 0) ? 
                          filter->service_end_handle : service->end_handle;
    
    osMutexRelease(g_ble_conn_state.mutex);
    
    // Discover characteristics
    // Note: rsi_ble_get_char_services is non-blocking but fills the response structure via callback
    rsi_ble_resp_char_services_t char_services = {0};
    int32_t ret = rsi_ble_get_char_services(device->addr.addr, start_handle, end_handle, &char_services);
    
    if (ret != 0) {
        LOG_DRV_ERROR("Failed to get char services: %d", ret);
        return AICAM_ERROR_HARDWARE;
    }
    
    // Wait for callback to complete
    if (osSemaphoreAcquire(g_ble_conn_state.discovery_sem, SL_BLE_DISCOVERY_TIMEOUT_MS) != osOK) {
        return AICAM_ERROR_TIMEOUT;
    }
    
    if (g_ble_conn_state.pending_resp_status != 0) {
        LOG_DRV_ERROR("Get char services status: %d", g_ble_conn_state.pending_resp_status);
        return AICAM_ERROR_HARDWARE;
    }
    
    // Copy discovered characteristics (char_services is filled by callback)
    uint8_t count = 0;
    uint8_t max_count = (*char_count < char_services.num_of_services) ? *char_count : char_services.num_of_services;
    
    for (int i = 0; i < max_count; i++) {
        char_serv_t *char_serv = &char_services.char_services[i];
        
        // Apply UUID filter if specified
        if (filter != NULL && filter->uuid_filter != NULL) {
            if (sl_ble_uuid_compare(&char_serv->char_data.char_uuid, filter->uuid_filter) != 0) {
                continue;
            }
        }
        
        characteristics[count].uuid = char_serv->char_data.char_uuid;
        characteristics[count].handle = char_serv->char_data.char_handle;
        characteristics[count].decl_handle = char_serv->handle;
        characteristics[count].properties = char_serv->char_data.char_property;
        characteristics[count].cccd_handle = 0; // Will be discovered separately if needed
        
        count++;
    }
    
    // Discover CCCD handles for characteristics that support notify/indicate
    for (int i = 0; i < count; i++) {
        if (characteristics[i].properties & (SL_BLE_CHAR_PROP_NOTIFY | SL_BLE_CHAR_PROP_INDICATE)) {
            uint16_t desc_start = characteristics[i].handle + 1;
            uint16_t desc_end = (i < count - 1) ? characteristics[i + 1].handle : service->end_handle;
            
            rsi_ble_resp_att_descs_t att_descs = {0};
            ret = rsi_ble_get_att_descriptors(device->addr.addr, desc_start, desc_end, &att_descs);
            
            if (ret == 0 && osSemaphoreAcquire(g_ble_conn_state.discovery_sem, SL_BLE_DISCOVERY_TIMEOUT_MS) == osOK) {
                if (g_ble_conn_state.pending_resp_status == 0) {
                    // Look for CCCD UUID (0x2902)
                    // Note: att_descs is filled by callback
                    uuid_t cccd_uuid = {.size = 16, .val.val16 = 0x2902};
                    for (int j = 0; j < att_descs.num_of_att; j++) {
                        if (sl_ble_uuid_compare(&att_descs.att_desc[j].att_type_uuid, &cccd_uuid) == 0) {
                            characteristics[i].cccd_handle = (att_descs.att_desc[j].handle[1] << 8) | att_descs.att_desc[j].handle[0];
                            break;
                        }
                    }
                }
            }
        }
    }
    
    *char_count = count;
    service->char_count = count;
    
    return AICAM_OK;
}

int sl_ble_enable_notify(sl_ble_conn_handle_t conn_handle, sl_ble_characteristic_t *characteristic,
                          sl_ble_notify_callback_t callback)
{
    if (characteristic == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    sl_ble_connected_device_t *device = sl_ble_find_device_by_handle(conn_handle);
    if (device == NULL || device->status != SL_BLE_CONN_STATUS_CONNECTED) {
        osMutexRelease(g_ble_conn_state.mutex);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (characteristic->cccd_handle == 0) {
        osMutexRelease(g_ble_conn_state.mutex);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Determine notify value based on properties
    uint16_t notify_value = 0;
    if (characteristic->properties & SL_BLE_CHAR_PROP_NOTIFY) {
        notify_value = 0x0001; // Enable notifications
    } else if (characteristic->properties & SL_BLE_CHAR_PROP_INDICATE) {
        notify_value = 0x0002; // Enable indications
    } else {
        osMutexRelease(g_ble_conn_state.mutex);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    osMutexRelease(g_ble_conn_state.mutex);
    
    // Write to CCCD
    uint8_t cccd_data[2] = {(uint8_t)(notify_value & 0xFF), (uint8_t)((notify_value >> 8) & 0xFF)};
    int32_t ret = rsi_ble_set_att_value(device->addr.addr, characteristic->cccd_handle, 2, cccd_data);
    
    if (ret != 0) {
        return AICAM_ERROR_HARDWARE;
    }
    
    // Wait for write response
    if (osSemaphoreAcquire(g_ble_conn_state.write_sem, SL_BLE_DISCOVERY_TIMEOUT_MS) != osOK) {
        return AICAM_ERROR_TIMEOUT;
    }
    
    if (g_ble_conn_state.pending_resp_status != 0) {
        return AICAM_ERROR_HARDWARE;
    }
    
    // Store callback (simplified - full implementation would use handle mapping)
    if (callback != NULL) {
        // Store callback for this characteristic
        // Note: This is a simplified implementation
        // Full implementation would maintain a proper handle-to-callback mapping
    }
    
    return AICAM_OK;
}

int sl_ble_write_characteristic(sl_ble_conn_handle_t conn_handle, sl_ble_characteristic_t *characteristic,
                                const uint8_t *data, uint16_t data_len, uint8_t write_without_response)
{
    if (characteristic == NULL || data == NULL || data_len == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    sl_ble_connected_device_t *device = sl_ble_find_device_by_handle(conn_handle);
    if (device == NULL || device->status != SL_BLE_CONN_STATUS_CONNECTED) {
        osMutexRelease(g_ble_conn_state.mutex);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    osMutexRelease(g_ble_conn_state.mutex);
    
    int32_t ret;
    if (write_without_response && (characteristic->properties & SL_BLE_CHAR_PROP_WRITE_NO_RESP)) {
        ret = rsi_ble_set_att_cmd(device->addr.addr, characteristic->handle, data_len, data);
        return (ret == 0) ? AICAM_OK : AICAM_ERROR_HARDWARE;
    } else {
        ret = rsi_ble_set_att_value(device->addr.addr, characteristic->handle, data_len, data);
        
        if (ret != 0) {
            return AICAM_ERROR_HARDWARE;
        }
        
        // Wait for write response
        if (osSemaphoreAcquire(g_ble_conn_state.write_sem, SL_BLE_DISCOVERY_TIMEOUT_MS) != osOK) {
            return AICAM_ERROR_TIMEOUT;
        }
        
        return (g_ble_conn_state.pending_resp_status == 0) ? AICAM_OK : AICAM_ERROR_HARDWARE;
    }
}

int sl_ble_read_characteristic(sl_ble_conn_handle_t conn_handle, sl_ble_characteristic_t *characteristic,
                               uint8_t *data, uint16_t *data_len)
{
    if (characteristic == NULL || data == NULL || data_len == NULL || *data_len == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    sl_ble_connected_device_t *device = sl_ble_find_device_by_handle(conn_handle);
    if (device == NULL || device->status != SL_BLE_CONN_STATUS_CONNECTED) {
        osMutexRelease(g_ble_conn_state.mutex);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    osMutexRelease(g_ble_conn_state.mutex);
    
    // Note: rsi_ble_get_att_value is non-blocking but fills the response structure via callback
    rsi_ble_resp_att_value_t att_val = {0};
    int32_t ret = rsi_ble_get_att_value(device->addr.addr, characteristic->handle, &att_val);
    
    if (ret != 0) {
        return AICAM_ERROR_HARDWARE;
    }
    
    // Wait for callback to complete
    if (osSemaphoreAcquire(g_ble_conn_state.read_sem, SL_BLE_DISCOVERY_TIMEOUT_MS) != osOK) {
        return AICAM_ERROR_TIMEOUT;
    }
    
    if (g_ble_conn_state.pending_resp_status != 0) {
        return AICAM_ERROR_HARDWARE;
    }
    
    // Copy data from stored response (att_val is also filled by callback, but we use stored copy)
    uint16_t copy_len = (g_ble_conn_state.pending_read_data.len < *data_len) ? 
                        g_ble_conn_state.pending_read_data.len : *data_len;
    memcpy(data, g_ble_conn_state.pending_read_data.att_value, copy_len);
    *data_len = copy_len;
    
    return AICAM_OK;
}

int sl_ble_get_connected_device(sl_ble_conn_handle_t conn_handle, sl_ble_connected_device_t *device)
{
    if (device == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    sl_ble_connected_device_t *dev = sl_ble_find_device_by_handle(conn_handle);
    if (dev == NULL) {
        osMutexRelease(g_ble_conn_state.mutex);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memcpy(device, dev, sizeof(sl_ble_connected_device_t));
    osMutexRelease(g_ble_conn_state.mutex);
    
    return AICAM_OK;
}

uint8_t sl_ble_is_connected(sl_ble_conn_handle_t conn_handle)
{
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    sl_ble_connected_device_t *device = sl_ble_find_device_by_handle(conn_handle);
    uint8_t connected = (device != NULL && device->status == SL_BLE_CONN_STATUS_CONNECTED) ? 1 : 0;
    
    osMutexRelease(g_ble_conn_state.mutex);
    
    return connected;
}

uint8_t sl_ble_connected_num(void)
{
    sl_ble_conn_init();
    osMutexAcquire(g_ble_conn_state.mutex, osWaitForever);
    
    uint8_t count = 0;
    for (int i = 0; i < SL_BLE_MAX_CONNECTIONS; i++) {
        if (g_ble_conn_state.devices[i].status == SL_BLE_CONN_STATUS_CONNECTED) {
            count++;
        }
    }
    osMutexRelease(g_ble_conn_state.mutex);
    return count;
}

#endif
