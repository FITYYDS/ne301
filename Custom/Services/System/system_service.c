/**
 * @file system_service.c
 * @brief System Management Service Implementation
 * @details Implements system-wide management including work modes, scheduling, power mode, etc.
 *         Integrated with json_config_mgr for configuration management
 */

 #include "system_service.h"
 #include "buffer_mgr.h"
 #include "debug.h"
 #include "drtc.h"
 #include "json_config_mgr.h"
 #include "device_service.h"
 #include "u0_module.h"
 #include "ms_bridging.h"
 #include "mqtt_service.h"
 #include <string.h>
 #include <stdlib.h>
 #include <time.h>
 #include "ai_service.h"
 #include "device_service.h"
 #include "service_init.h"
 #include "sl_net_netif.h"
 #include "communication_service.h"
 #include "web_server.h"
 
 
 /* ==================== System Controller Implementation ==================== */
 
 struct system_controller_s {
     system_state_t current_state;        // Current system state
     system_state_t previous_state;       // Previous system state
     system_event_callback_t callback;    // State change callback
     void *callback_user_data;            // Callback user data
     uint64_t state_change_time;          // Last state change timestamp
     uint32_t state_change_count;         // State change counter
     bool is_initialized;                 // Initialization status
     
     // Power mode management
     power_mode_config_t power_config;    // Power mode configuration
     power_mode_change_callback_t power_callback;  // Power mode change callback
     power_mode_features_t power_features;    // Power mode features
     void *power_callback_user_data;      // Power callback user data
     
     // Work mode management
     aicam_work_mode_t current_work_mode; // Current work mode
     work_mode_config_t work_config;      // Work mode configuration
     work_mode_change_callback_t work_callback; // Work mode change callback
     void *work_callback_user_data;       // Work callback user data
     
     // Capture trigger management
     capture_trigger_callback_t capture_callback; // Capture trigger callback
     void *capture_callback_user_data;    // Capture callback user data
     
     // Timer trigger management
     aicam_bool_t timer_trigger_active;    // Timer trigger active status
     char timer_task_name[32];             // Timer task name for RTC registration
     uint32_t timer_task_count;            // Timer task execution counter
     
     // Wakeup source management
     wakeup_source_config_t wakeup_sources[WAKEUP_SOURCE_MAX]; // Wakeup source configurations
     uint32_t activity_counter;            // Activity counter for power management
 };

 struct system_service_context_s {
    system_controller_t *controller;       // System controller
    bool is_initialized;                   // Initialization status
    bool is_started;                       // Service started status
    bool timer_trigger_configured;         // Timer trigger configuration status
    bool timer_trigger_active;             // Timer trigger active status
    
    // U0 module integration
    uint32_t last_wakeup_flag;             // Last wakeup flag from U0
    bool task_completed;                   // Task completion flag for sleep decision
    bool sleep_pending;                    // Sleep operation pending flag
};


 static struct system_service_context_s g_system_service_ctx = {0};
static volatile aicam_bool_t g_capture_in_progress = AICAM_FALSE;
static volatile aicam_bool_t g_fast_fail_mqtt_policy = AICAM_FALSE;
static const system_capture_request_t g_capture_defaults = {
    .enable_ai = AICAM_TRUE,
    .chunk_size = 0,        // auto chunk
    .store_to_sd = AICAM_TRUE,
    .fast_fail_mqtt = AICAM_FALSE
};
 
 static uint64_t get_timestamp_ms(void)
 {
     return rtc_get_timestamp_ms();
 }
 
 /**
  * @brief Initialize default wakeup source configurations
  */
 static void init_default_wakeup_sources(wakeup_source_config_t *wakeup_sources)
 {
     if (!wakeup_sources) return;
     
     // IO wakeup source
     wakeup_sources[WAKEUP_SOURCE_IO] = (wakeup_source_config_t){
         .enabled = AICAM_TRUE,
         .low_power_supported = AICAM_FALSE,
         .full_speed_supported = AICAM_TRUE,
         .debounce_ms = 50,
         .config_data = NULL
     };
     
     // RTC wakeup source
     wakeup_sources[WAKEUP_SOURCE_RTC] = (wakeup_source_config_t){
         .enabled = AICAM_TRUE,
         .low_power_supported = AICAM_TRUE,
         .full_speed_supported = AICAM_TRUE,
         .debounce_ms = 0,
         .config_data = NULL
     };
     
     // PIR wakeup source
     wakeup_sources[WAKEUP_SOURCE_PIR] = (wakeup_source_config_t){
         .enabled = AICAM_TRUE,
         .low_power_supported = AICAM_FALSE,
         .full_speed_supported = AICAM_TRUE,
         .debounce_ms = 100,
         .config_data = NULL
     };
     
     // Button wakeup source
     wakeup_sources[WAKEUP_SOURCE_BUTTON] = (wakeup_source_config_t){
         .enabled = AICAM_TRUE,
         .low_power_supported = AICAM_TRUE,
         .full_speed_supported = AICAM_TRUE,
         .debounce_ms = 200,
         .config_data = NULL
     };
     
     // Remote wakeup source
     wakeup_sources[WAKEUP_SOURCE_REMOTE] = (wakeup_source_config_t){
         .enabled = AICAM_TRUE,
         .low_power_supported = AICAM_TRUE,
         .full_speed_supported = AICAM_TRUE,
         .debounce_ms = 0,
         .config_data = NULL
     };
 }
 
 /**
  * @brief Initialize power mode configuration with defaults
  */
 static void init_default_power_config(power_mode_config_t *config)
 {
     if (!config) return;
     
     // Device defaults to low power mode
     config->current_mode = POWER_MODE_LOW_POWER;
     config->default_mode = POWER_MODE_LOW_POWER;
     
     // Return to low power mode after 1 minute of inactivity
     config->low_power_timeout_ms = 60000; // 60 seconds
     config->last_activity_time = get_timestamp_ms();
     config->mode_switch_count = 0;
 }
 
 /**
  * @brief Load power mode configuration from NVS
  */
 static aicam_result_t load_power_mode_config_from_nvs(power_mode_config_t *config)
 {
     if (!config) return AICAM_ERROR_INVALID_PARAM;
     
     // Try to load from NVS
     power_mode_config_t* power_config = NULL;
     aicam_result_t result = json_config_get_power_mode_config(power_config);
     if (result == AICAM_OK) {
         memcpy(config, power_config, sizeof(power_mode_config_t));
         LOG_SVC_INFO("Power mode configuration loaded from NVS: current=%u, default=%u", 
                      config->current_mode, config->default_mode);
         return AICAM_OK;
     }
     
     // If loading failed, use default configuration
     LOG_SVC_WARN("Failed to load power mode config from NVS, using defaults: %d", result);
     init_default_power_config(config);
     
     // Save default configuration to NVS
     result = json_config_set_power_mode_config(config);
     if (result != AICAM_OK) {
         LOG_SVC_ERROR("Failed to save default power mode config to NVS: %d", result);
     }
     
     return AICAM_OK;
 }
 
 system_controller_t* system_controller_create(void)
 {
     system_controller_t *controller = buffer_calloc(1, sizeof(system_controller_t));
     if (!controller) {
         LOG_SVC_ERROR("Failed to allocate system controller");
         return NULL;
     }
     
     controller->current_state = SYSTEM_STATE_INIT;
     controller->previous_state = SYSTEM_STATE_INIT;
     controller->callback = NULL;
     controller->callback_user_data = NULL;
     controller->state_change_time = get_timestamp_ms();
     controller->state_change_count = 0;
     controller->is_initialized = false;
     
     // Initialize power mode configuration from NVS
     aicam_result_t power_result = load_power_mode_config_from_nvs(&controller->power_config);
     if (power_result != AICAM_OK) {
         LOG_SVC_ERROR("Failed to load power mode configuration: %d", power_result);
         // Continue with default configuration
     }
     controller->power_callback = NULL;
     controller->power_callback_user_data = NULL;
     
     // Initialize work mode management
     controller->current_work_mode = AICAM_WORK_MODE_IMAGE; // Default to image mode
     memset(&controller->work_config, 0, sizeof(work_mode_config_t));
     controller->work_callback = NULL;
     controller->work_callback_user_data = NULL;
     
     // Initialize capture trigger management
     controller->capture_callback = NULL;
     controller->capture_callback_user_data = NULL;
     
     // Initialize timer trigger management
     controller->timer_trigger_active = AICAM_FALSE;
     memset(controller->timer_task_name, 0, sizeof(controller->timer_task_name));
     controller->timer_task_count = 0;
     
     // Initialize wakeup source management
     init_default_wakeup_sources(controller->wakeup_sources);
     controller->activity_counter = 0;
     
     return controller;
 }
 
 void system_controller_destroy(system_controller_t *controller)
 {
     if (controller) {
         system_controller_deinit(controller);
         buffer_free(controller);
     }
 }
 
 aicam_result_t system_controller_init(system_controller_t *controller)
 {
     if (!controller) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     if (controller->is_initialized) {
         return AICAM_OK; // Already initialized
     }
     
     controller->current_state = SYSTEM_STATE_INIT;
     controller->previous_state = SYSTEM_STATE_INIT;
     controller->state_change_time = get_timestamp_ms();
     controller->state_change_count = 0;
     controller->is_initialized = true;
     
     LOG_SVC_INFO("System controller initialized");
     return AICAM_OK;
 }
 
 void system_controller_deinit(system_controller_t *controller)
 {
     if (!controller || !controller->is_initialized) {
         return;
     }
     
     controller->is_initialized = false;
     controller->callback = NULL;
     controller->callback_user_data = NULL;
     
     // Clear power mode callbacks
     controller->power_callback = NULL;
     controller->power_callback_user_data = NULL;
     
     // Clear work mode callbacks
     controller->work_callback = NULL;
     controller->work_callback_user_data = NULL;
     
     // Clear capture trigger callbacks
     controller->capture_callback = NULL;
     controller->capture_callback_user_data = NULL;
     
     // Clear timer trigger management
     if (controller->timer_trigger_active && strlen(controller->timer_task_name) > 0) {
         rtc_unregister_task_by_name(controller->timer_task_name);
         controller->timer_trigger_active = AICAM_FALSE;
     }
     
     LOG_SVC_INFO("System controller deinitialized");
 }
 
 system_state_t system_controller_get_state(system_controller_t *controller)
 {
     if (!controller || !controller->is_initialized) {
         return SYSTEM_STATE_ERROR;
     }
     return controller->current_state;
 }
 
 aicam_result_t system_controller_set_state(system_controller_t *controller, 
                                           system_state_t new_state)
 {
     if (!controller || !controller->is_initialized) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     if (new_state >= SYSTEM_STATE_MAX) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     if (controller->current_state == new_state) {
         return AICAM_OK; // No change needed
     }
     
     system_state_t old_state = controller->current_state;
     
     // Update state
     controller->previous_state = controller->current_state;
     controller->current_state = new_state;
     controller->state_change_time = get_timestamp_ms();
     controller->state_change_count++;
     
     LOG_SVC_INFO("System state changed: %d -> %d", old_state, new_state);
     
     // Notify callback if registered
     if (controller->callback) {
         controller->callback(old_state, new_state, controller->callback_user_data);
     }
     
     return AICAM_OK;
 }
 
 aicam_result_t system_controller_register_callback(system_controller_t *controller,
                                                   system_event_callback_t callback,
                                                   void *user_data)
 {
     if (!controller || !controller->is_initialized) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     controller->callback = callback;
     controller->callback_user_data = user_data;
     
     return AICAM_OK;
 }
 
 /* ==================== Power Mode Management Implementation ==================== */
 
 power_mode_t system_controller_get_power_mode(system_controller_t *controller)
 {
     if (!controller || !controller->is_initialized) {
         return POWER_MODE_LOW_POWER; // Default to low power mode
     }
     return controller->power_config.current_mode;
 }
 
 aicam_result_t system_controller_set_power_mode(system_controller_t *controller,
                                                power_mode_t mode,
                                                power_trigger_type_t trigger_type)
 {
     if (!controller || !controller->is_initialized) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     if (mode >= POWER_MODE_MAX) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     if (controller->power_config.current_mode == mode) {
         return AICAM_OK; // No change needed
     }
     
     power_mode_t old_mode = controller->power_config.current_mode;
     
     // Update power mode
     controller->power_config.current_mode = mode;
     controller->power_config.last_activity_time = get_timestamp_ms();
     controller->power_config.mode_switch_count++;
     
     // Save power mode configuration to NVS
     aicam_result_t save_result = json_config_set_power_mode_config(&controller->power_config);
     if (save_result != AICAM_OK) {
         LOG_SVC_ERROR("Failed to save power mode configuration to NVS: %d", save_result);
         // Continue execution even if save fails
     }
     
     const char *mode_names[] = {"LOW_POWER", "FULL_SPEED"};
     const char *trigger_names[] = {"MANUAL", "AUTO_WAKEUP", "TIMEOUT"};
     
     LOG_SVC_INFO("Power mode changed: %s -> %s (trigger: %s)", 
                  mode_names[old_mode], 
                  mode_names[mode],
                  trigger_names[trigger_type]);
     
     // Notify callback if registered
     if (controller->power_callback) {
         controller->power_callback(old_mode, mode, trigger_type, controller->power_callback_user_data);
     }
     
     // Handle mode-specific logic
     switch (mode) {
         case POWER_MODE_LOW_POWER:
            // Low power mode: may need to disable some features, maintain basic functionality
            LOG_SVC_DEBUG("Entering low power mode - conserving power");
            // TODO: Add specific low power control logic here
             break;
             
         case POWER_MODE_FULL_SPEED:
            // Full speed mode: enable all features
            LOG_SVC_DEBUG("Entering full speed mode - all features active");
            // TODO: Add specific full speed mode control logic here
             break;
             
         default:
             break;
     }
     
     return AICAM_OK;
 }
 
 aicam_result_t system_controller_get_power_config(system_controller_t *controller,
                                                  power_mode_config_t *config)
 {
     if (!controller || !controller->is_initialized || !config) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     *config = controller->power_config;
     return AICAM_OK;
 }
 
 aicam_result_t system_controller_set_power_config(system_controller_t *controller,
                                                  const power_mode_config_t *config)
 {
     if (!controller || !controller->is_initialized || !config) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     // Validate configuration
     if (config->current_mode >= POWER_MODE_MAX || config->default_mode >= POWER_MODE_MAX) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     power_mode_t old_mode = controller->power_config.current_mode;
     controller->power_config = *config;
     
     // If current mode changed, trigger callback
     if (old_mode != config->current_mode && controller->power_callback) {
         controller->power_callback(old_mode, config->current_mode, 
                                   POWER_TRIGGER_MANUAL, controller->power_callback_user_data);
     }
     
     LOG_SVC_DEBUG("Power mode configuration updated");
     return AICAM_OK;
 }
 
 aicam_result_t system_controller_register_power_callback(system_controller_t *controller,
                                                         power_mode_change_callback_t callback,
                                                         void *user_data)
 {
     if (!controller || !controller->is_initialized) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     controller->power_callback = callback;
     controller->power_callback_user_data = user_data;
     
     return AICAM_OK;
 }
 
 aicam_result_t system_controller_update_activity(system_controller_t *controller)
 {
     if (!controller || !controller->is_initialized) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     controller->power_config.last_activity_time = get_timestamp_ms();
     
     // If currently in low power mode and there is activity, may need to switch to full speed mode
    //  if (controller->power_config.current_mode == POWER_MODE_LOW_POWER) {
    //      // Automatically switch to full speed mode when device is woken up (based on trigger conditions in diagram)
    //      system_controller_set_power_mode(controller, POWER_MODE_FULL_SPEED, POWER_TRIGGER_AUTO_WAKEUP);
    //  }
     
     return AICAM_OK;
 }
 
 aicam_result_t system_controller_check_power_timeout(system_controller_t *controller)
 {
     if (!controller || !controller->is_initialized) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     // Only check timeout in full speed mode
     if (controller->power_config.current_mode != POWER_MODE_FULL_SPEED) {
         return AICAM_OK;
     }
     
     uint64_t current_time = get_timestamp_ms();
     uint64_t elapsed_time = current_time - controller->power_config.last_activity_time;
     
     // Check if 1 minute timeout with no activity
     if (elapsed_time >= controller->power_config.low_power_timeout_ms) {
         LOG_SVC_INFO("Power mode timeout reached (%lu ms), switching to low power mode", (unsigned long)elapsed_time);
         return system_controller_set_power_mode(controller, POWER_MODE_LOW_POWER, POWER_TRIGGER_TIMEOUT);
     }
     return AICAM_OK;
     
 }
 
 // system_controller_set_power_feature function removed - use wakeup source configuration instead
 
 /* ==================== Wakeup Source Management Helper Functions ==================== */
 
 /**
  * @brief Check if wakeup source is supported in current power mode
  */
 static aicam_bool_t is_wakeup_source_supported(system_controller_t *controller, wakeup_source_type_t source)
 {
     if (!controller || source >= WAKEUP_SOURCE_MAX) {
         return AICAM_FALSE;
     }
     
     wakeup_source_config_t *config = &controller->wakeup_sources[source];
     
     if (!config->enabled) {
         return AICAM_FALSE;
     }
     
     switch (controller->power_config.current_mode) {
         case POWER_MODE_LOW_POWER:
             return config->low_power_supported;
         case POWER_MODE_FULL_SPEED:
             return config->full_speed_supported;
         default:
             return AICAM_FALSE;
     }
 }
 
 /**
  * @brief Update activity timestamp and counter
  */
 static void update_activity(system_controller_t *controller)
 {
     if (!controller) return;
     
     controller->power_config.last_activity_time = get_timestamp_ms();
     controller->activity_counter++;
 }
 
/**
 * @brief Handle wakeup event
 * @details Handles different wakeup sources with specific behaviors:
 *          - Short press: LED slow blink, take photo
 *          - Long press (2s): LED solid on, enable AP
 *          - Super long press (10s): Factory reset
 *          - Other sources: Default behavior
 */
static aicam_result_t handle_wakeup_event(system_controller_t *controller, wakeup_source_type_t source)
{
    if (!controller) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    LOG_SVC_INFO("Wakeup event from source: %d", source);
    
    // Update activity
    update_activity(controller);
    
    // Set system state to active
    aicam_result_t result = system_controller_set_state(controller, SYSTEM_STATE_ACTIVE);
    if (result != AICAM_OK) {
        return result;
    }
    
    // Handle different button press types with specific behaviors
    switch (source) {
        case WAKEUP_SOURCE_BUTTON_SUPER_LONG:
            // Super long press (10s) - Factory reset (highest priority)
            LOG_SVC_INFO("Button super long press (10s) - triggering factory reset");
            // Factory reset function will set fast blink LED internally
            device_service_reset_to_factory_defaults();
            // Note: device_service_reset_to_factory_defaults() will restart the system
            break;
            
        case WAKEUP_SOURCE_BUTTON_LONG:
            // Long press (2s) - Enable AP, LED solid on
            LOG_SVC_INFO("Button long press (2s) - enabling AP");
            device_service_set_indicator_state(SYSTEM_INDICATOR_RUNNING_AP_ON);
            // // Reset AP sleep timer and start AP if not running
            // web_server_ap_sleep_timer_reset();
            // if (communication_is_interface_connected(NETIF_NAME_WIFI_AP) == AICAM_FALSE) {
            //     communication_start_interface(NETIF_NAME_WIFI_AP);
            // }
            break;
            
        case WAKEUP_SOURCE_BUTTON:
            // Short press - Take photo, LED slow blink (AP off state)
            LOG_SVC_INFO("Button short press - taking photo");
            // Set LED to slow blink (system running, AP may be off)
        
            device_service_set_indicator_state(SYSTEM_INDICATOR_RUNNING_AP_OFF);
            
            // Execute capture callback
            if (controller->capture_callback) {
                controller->capture_callback(CAPTURE_TRIGGER_BUTTON, controller->capture_callback_user_data);
            }
            break;
            
        default:
            // Other wakeup sources - default behavior
            // Set LED based on current AP state
            if (communication_is_interface_connected(NETIF_NAME_WIFI_AP)) {
                device_service_set_indicator_state(SYSTEM_INDICATOR_RUNNING_AP_ON);
            } else {
                device_service_set_indicator_state(SYSTEM_INDICATOR_RUNNING_AP_OFF);
            }
            // Execute capture callback if registered
            if (controller->capture_callback) {
                capture_trigger_type_t trigger_type = (capture_trigger_type_t)source;
                controller->capture_callback(trigger_type, controller->capture_callback_user_data);
            }
            break;
    }
    
    return AICAM_OK;
}
 
 /* ==================== Work Mode Management Helper Functions ==================== */
 
 /**
  * @brief Save work mode configuration to persistent storage
  * @param controller System controller handle
  * @return AICAM_OK on success
  */
 static aicam_result_t save_work_mode_config_to_nvs(system_controller_t *controller)
 {
     if (!controller || !controller->is_initialized) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     // Save configuration to json_config_mgr (includes NVS persistence)
     aicam_result_t config_result = json_config_set_work_mode_config(&controller->work_config);
     if (config_result != AICAM_OK) {
         LOG_SVC_ERROR("Failed to save work mode config: %d", config_result);
         return config_result;
     }
     
     LOG_SVC_DEBUG("Work mode configuration saved successfully");
     return AICAM_OK;
 }
 
 /* ==================== Work Mode Management Implementation ==================== */
 static aicam_result_t apply_timer_trigger_config(system_controller_t *controller,
                                                  const timer_trigger_config_t *timer_config);
 
 aicam_work_mode_t system_controller_get_work_mode(system_controller_t *controller)
 {
     if (!controller || !controller->is_initialized) {
         return AICAM_WORK_MODE_IMAGE; // Default to image mode
     }
     return controller->current_work_mode;
 }
 
 aicam_result_t system_controller_set_work_mode(system_controller_t *controller,
                                               aicam_work_mode_t mode)
 {
     if (!controller || !controller->is_initialized) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     if (mode >= AICAM_WORK_MODE_MAX) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     if (controller->current_work_mode == mode) {
         return AICAM_OK; // No change needed
     }
     
     aicam_work_mode_t old_mode = controller->current_work_mode;
     
     // Update work mode
     controller->current_work_mode = mode;
     controller->work_config.work_mode = mode;
     
     const char *mode_names[] = {"IMAGE", "VIDEO_STREAM"};
     LOG_SVC_INFO("Work mode changed: %s -> %s", 
                  mode_names[old_mode], 
                  mode_names[mode]);
     
     // Save configuration to persistent storage
     aicam_result_t config_result = save_work_mode_config_to_nvs(controller);
     if (config_result != AICAM_OK) {
         LOG_SVC_ERROR("Failed to save work mode configuration persistently: %d", config_result);
         // Continue execution even if save fails, as the mode change is already applied
     }
     
     // Notify callback if registered
     if (controller->work_callback) {
         controller->work_callback(old_mode, mode, controller->work_callback_user_data);
     }
     
     return AICAM_OK;
 }
 
 aicam_result_t system_controller_get_work_config(system_controller_t *controller,
                                                 work_mode_config_t *config)
 {
     if (!controller || !controller->is_initialized || !config) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     *config = controller->work_config;
     return AICAM_OK;
 }
 
 aicam_result_t system_controller_set_work_config(system_controller_t *controller,
                                                 const work_mode_config_t *config)
 {
     if (!controller || !controller->is_initialized || !config) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     aicam_work_mode_t old_mode = controller->current_work_mode;
     
     // Update configuration
     memcpy(&controller->work_config, config, sizeof(work_mode_config_t));
     controller->current_work_mode = config->work_mode;
 
     //log timer trigger configuration
     LOG_SVC_INFO("Timer trigger configuration: %s", config->timer_trigger.enable ? "enabled" : "disabled");
     LOG_SVC_INFO("Timer trigger capture mode: %d", config->timer_trigger.capture_mode);
     LOG_SVC_INFO("Timer trigger interval: %d", config->timer_trigger.interval_sec);
     LOG_SVC_INFO("Timer trigger time nodes: %d", config->timer_trigger.time_node_count);
     for (int i = 0; i < config->timer_trigger.time_node_count; i++) {
         LOG_SVC_INFO("Timer trigger time node %d: %d", i, config->timer_trigger.time_node[i]);
     }
     for (int i = 1; i < config->timer_trigger.time_node_count; i++) {
         LOG_SVC_INFO("Timer trigger weekdays %d: %d", i, config->timer_trigger.weekdays[i]);
     }
 
     
     // Save configuration to persistent storage
     aicam_result_t config_result = save_work_mode_config_to_nvs(controller);
     if (config_result != AICAM_OK) {
         LOG_SVC_ERROR("Failed to save work mode configuration persistently: %d", config_result);
         // Continue execution even if save fails, as the configuration change is already applied
     }
     
     // Apply timer trigger configuration whenever work mode changes
     aicam_result_t timer_result = apply_timer_trigger_config(controller, &config->timer_trigger);
     if (timer_result != AICAM_OK) {
         LOG_SVC_ERROR("Failed to apply timer trigger configuration: %d", timer_result);
     }
     
     // If work mode changed, trigger callback
     if (old_mode != config->work_mode && controller->work_callback) {
         controller->work_callback(old_mode, config->work_mode, controller->work_callback_user_data);
     }
     
     LOG_SVC_DEBUG("Work mode configuration updated");
     return AICAM_OK;
 }
 
 aicam_result_t system_controller_register_work_callback(system_controller_t *controller,
                                                        work_mode_change_callback_t callback,
                                                        void *user_data)
 {
     if (!controller || !controller->is_initialized) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     controller->work_callback = callback;
     controller->work_callback_user_data = user_data;
     
     return AICAM_OK;
 }
 
 /* ==================== Timer Trigger Implementation ==================== */

/**
 * @brief Map weekdays to bits
 * @param weekdays Weekdays
 * @return Bits
 */
 static int map_weekdays_to_bits(uint8_t weekdays)
 {
     if (weekdays == 0) {
         return WEEKDAYS_ALL;
     }
     return (1 << (weekdays - 1));
 }
 
 /**
  * @brief Timer trigger callback function for RTC scheduled tasks
  * @param user_data Pointer to system controller
  */
 static void timer_trigger_callback(void *user_data)
 {
     system_controller_t *controller = (system_controller_t *)user_data;
     if (!controller || !controller->is_initialized) {
         LOG_SVC_ERROR("Invalid controller in timer trigger callback");
         return;
     }
     
     controller->timer_task_count++;
     LOG_SVC_INFO("Timer trigger activated (count: %lu)", controller->timer_task_count);
     
     // Call the registered capture callback if available
     if (controller->capture_callback) {
         controller->capture_callback(CAPTURE_TRIGGER_RTC, controller->capture_callback_user_data);
     }
     else {
         LOG_SVC_ERROR("No capture callback registered");
     }
     
     // Update activity time to prevent power mode timeout during capture
     system_controller_update_activity(controller);
 }
 
 /**
  * @brief Apply timer trigger configuration
  * @param controller System controller handle
  * @param timer_config Timer trigger configuration
  * @return AICAM_OK on success
  */
 static aicam_result_t apply_timer_trigger_config(system_controller_t *controller,
                                                 const timer_trigger_config_t *timer_config)
 {
     if (!controller || !controller->is_initialized || !timer_config) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     // Stop existing timer trigger if active
     if (controller->timer_trigger_active && strlen(controller->timer_task_name) > 0) {
         rtc_unregister_task_by_name(controller->timer_task_name);
         controller->timer_trigger_active = AICAM_FALSE;
         LOG_SVC_INFO("Stopped existing timer trigger: %s", controller->timer_task_name);
     }
     
     // If timer trigger is disabled, just return
     if (!timer_config->enable) {
         LOG_SVC_INFO("Timer trigger is disabled");
         return AICAM_OK;
     }
     
     // Generate unique task name
     snprintf(controller->timer_task_name, sizeof(controller->timer_task_name),
              "timer_capture_%lu", (unsigned long)rtc_get_timeStamp() % 10000);
     
     aicam_result_t result = AICAM_OK;
     
     switch (timer_config->capture_mode) {
         case AICAM_TIMER_CAPTURE_MODE_INTERVAL:
             // Interval mode: capture at regular intervals
             result = system_controller_register_rtc_trigger(controller,
                                                            WAKEUP_TYPE_INTERVAL,
                                                            controller->timer_task_name,
                                                            timer_config->interval_sec,   //interval_sec
                                                            0,  // day_offset
                                                            0,  // weekdays
                                                            REPEAT_INTERVAL,
                                                            timer_trigger_callback,
                                                            controller);   //user_data
             break;
             
         case AICAM_TIMER_CAPTURE_MODE_ABSOLUTE:
             // One-time mode: capture once at specified time
             if (timer_config->time_node_count > 0) {
                 //should effective to every time node in the array
                 for (int i = 0; i < timer_config->time_node_count; i++) {
                     uint64_t trigger_time = timer_config->time_node[i];
                     result = system_controller_register_rtc_trigger(controller,
                                                                WAKEUP_TYPE_ABSOLUTE,
                                                                controller->timer_task_name,
                                                                trigger_time,
                                                                0,  // day_offset
                                                                map_weekdays_to_bits(timer_config->weekdays[i]),
                                                                timer_config->weekdays[i] == 0 ? REPEAT_DAILY : REPEAT_WEEKLY,
                                                                timer_trigger_callback,
                                                                controller);
                     if (result != AICAM_OK) {
                         LOG_SVC_ERROR("Failed to register RTC capture trigger: %d", result);
                         return result;
                     }
                 }
             } else {
                 LOG_SVC_ERROR("Timer trigger once mode requires at least one time node");
                 result = AICAM_ERROR_INVALID_PARAM;
             }
             break;
             
         default:
             LOG_SVC_ERROR("Unsupported timer capture mode: %d", timer_config->capture_mode);
             result = AICAM_ERROR_NOT_SUPPORTED;
             break;
     }
     
     if (result == AICAM_OK) {
         controller->timer_trigger_active = AICAM_TRUE;
         controller->timer_task_count = 0;
     }
     
     return result;
 }
 
/* ==================== Capture Trigger Management Implementation ==================== */



/**
 * @brief Async wakeup task - handles image capture and upload based on work mode
 * @param controller System controller pointer
 */
 static void wakeup_task_async(system_controller_t *controller)
 {
     LOG_SVC_INFO("=== Wakeup Task Started ===");
     LOG_SVC_INFO("Current work mode: %d", controller->current_work_mode);


     // Check current work mode
     if (controller->current_work_mode == AICAM_WORK_MODE_IMAGE)
     {
        LOG_SVC_INFO("Image mode detected - starting capture and upload to MQTT");
        
        //Use the new unified interface for capture and upload
        system_capture_request_t req = {
            .enable_ai = AICAM_TRUE,
            .chunk_size = 0,
            .store_to_sd = AICAM_TRUE
        };
        aicam_result_t ret = system_service_capture_request(&req, NULL);
        
        if (ret == AICAM_OK) {
            LOG_SVC_INFO("Image capture and upload completed successfully");
        } else {
            LOG_SVC_ERROR("Image capture and upload failed: %d", ret);
        }
     }
     else if (controller->current_work_mode == AICAM_WORK_MODE_VIDEO_STREAM)
     {
         LOG_SVC_INFO("Video stream mode detected - no action (future: stream to remote)");
         // TODO: Future implementation for video streaming
         // This would involve:
         // 1. Starting video stream
         // 2. Establishing remote connection (RTSP/etc)
         // 3. Streaming video frames
         LOG_SVC_WARN("Video streaming not yet implemented");
     }
     else
     {
         LOG_SVC_WARN("Unknown work mode: %d", controller->current_work_mode);
     }

     LOG_SVC_INFO("=== Wakeup Task Completed ===");

     // Update activity timestamp
     system_controller_update_activity(controller);
 }

/**
 * @brief Default capture callback function framework
 * @details This is a simple framework demonstrating how to handle capture triggers
 *          Users can register their own callback or extend this implementation
 * @param trigger_type Type of trigger that initiated the capture
 * @param user_data User data passed during callback registration
 */
static void default_capture_callback(capture_trigger_type_t trigger_type, void *user_data)
{
    (void)user_data; // Unused in default implementation

    system_controller_t *controller = (system_controller_t *)user_data;
    if(!controller) {
        LOG_SVC_ERROR("Controller is NULL");
        return;
    }
    
    const char *trigger_names[] = {
        "IO",
        "RTC_WAKEUP",
        "PIR",
        "BUTTON",
        "REMOTE",
        "WUFI",
        "RTC"
    };
    
    LOG_SVC_INFO("=== Capture Trigger Activated ===");
    LOG_SVC_INFO("Trigger Type: %s (%d)", 
                 (trigger_type < sizeof(trigger_names)/sizeof(trigger_names[0])) ? trigger_names[trigger_type] : "UNKNOWN",
                 trigger_type);
    
    // Handle different trigger types
    switch (trigger_type) {
        case CAPTURE_TRIGGER_IO:
            LOG_SVC_INFO("IO trigger detected - starting capture sequence");
            // TODO: Implement IO trigger capture logic
            // Example: trigger_image_capture(CAPTURE_MODE_IO);
            break;
            
        case CAPTURE_TRIGGER_RTC_WAKEUP:
            LOG_SVC_INFO("RTC wakeup trigger detected - scheduled capture");
            wakeup_task_async(controller);
            LOG_SVC_INFO("RTC wakeup trigger detected - scheduled capture completed");
            system_service_task_completed();
            break;
        
        case CAPTURE_TRIGGER_RTC:
            LOG_SVC_INFO("RTC timer trigger detected - scheduled capture");
            wakeup_task_async(controller);
            LOG_SVC_INFO("RTC timer trigger detected - scheduled capture completed");
            break;
            
        case CAPTURE_TRIGGER_PIR:
            LOG_SVC_INFO("PIR motion detected - starting video recording");
            // TODO: Implement PIR trigger capture logic
            // Example: trigger_motion_recording();
            break;
            
        case CAPTURE_TRIGGER_BUTTON:
            LOG_SVC_INFO("Button pressed - manual capture");
            wakeup_task_async(controller);
            system_service_task_completed();
            // TODO: Implement button trigger capture logic
            // Example: trigger_manual_capture();
            break;
            
        case CAPTURE_TRIGGER_REMOTE:
            LOG_SVC_INFO("Remote trigger detected - network capture");
            // TODO: Implement remote trigger capture logic
            // Example: trigger_remote_capture();
            break;

        case CAPTURE_TRIGGER_WUFI:
            LOG_SVC_INFO("WUFI trigger detected - WUFI capture");
            // TODO: Implement WUFI trigger capture logic
            // Example: trigger_wufi_capture();
            break;
        default:
            LOG_SVC_WARN("Unknown trigger type: %d", trigger_type);
            break;
    }
    
    LOG_SVC_INFO("=== Capture Sequence Initiated ===");
    
    // Mark task as completed for sleep management
    // Note: In real implementation, this should be called after capture actually completes
 
}

/**
 * @brief Register capture callback function
 * @param callback Capture callback function (NULL to use default)
 * @param user_data User data to pass to callback
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_register_capture_callback(capture_trigger_callback_t callback, void *user_data)
{
    if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    system_controller_t *controller = g_system_service_ctx.controller;
    
    // Use default callback if NULL provided
    controller->capture_callback = (callback != NULL) ? callback : default_capture_callback;
    controller->capture_callback_user_data = user_data;
    
    LOG_SVC_INFO("Capture callback registered: %s", 
                 (callback != NULL) ? "custom" : "default");
    
    return AICAM_OK;
}

/**
 * @brief Unregister capture callback function
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_unregister_capture_callback(void)
{
    if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    system_controller_t *controller = g_system_service_ctx.controller;
    controller->capture_callback = NULL;
    controller->capture_callback_user_data = NULL;
    
    LOG_SVC_INFO("Capture callback unregistered");
    return AICAM_OK;
}

/**
 * @brief Trigger capture manually
 * @param trigger_type Trigger type for this capture
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_trigger_capture(capture_trigger_type_t trigger_type)
{
    if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    system_controller_t *controller = g_system_service_ctx.controller;
    
    if (!controller->capture_callback) {
        LOG_SVC_WARN("No capture callback registered, using default");
        controller->capture_callback = default_capture_callback;
    }
    
    LOG_SVC_INFO("Manually triggering capture: type=%d", trigger_type);
    controller->capture_callback(trigger_type, controller->capture_callback_user_data);
    
    return AICAM_OK;
}

aicam_result_t system_controller_register_io_trigger(system_controller_t *controller,
                                                     int io_pin,
                                                     int trigger_mode,
                                                     capture_trigger_callback_t callback,
                                                     void *user_data)
 {
     if (!controller || !controller->is_initialized || !callback) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     // Store callback for IO triggers
     controller->capture_callback = callback;
     controller->capture_callback_user_data = user_data;
     
 
     // Here you would typically configure the actual IO hardware
     // This is a simplified implementation
     LOG_SVC_INFO("IO trigger registered: pin=%d, mode=%d", io_pin, trigger_mode);
     
     return AICAM_OK;
 }
 
 aicam_result_t system_controller_register_rtc_trigger(system_controller_t *controller,
                                                      wakeup_type_t type,
                                                      const char *name,
                                                      uint64_t trigger_sec,
                                                      int16_t day_offset,
                                                      uint8_t weekdays,
                                                      repeat_type_t repeat,
                                                      timer_trigger_callback_t callback,
                                                      void *user_data)
 {
     if (!controller || !controller->is_initialized || !name || !callback) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     // Store callback for RTC triggers
     // controller->capture_callback = NULL;
     // controller->capture_callback_user_data = NULL;
     
     // Create rtc_wakeup_t structure for scheduled capture
     rtc_wakeup_t wakeup = {0};
     strncpy(wakeup.name, name, sizeof(wakeup.name) - 1);
     wakeup.type = type;
     wakeup.repeat = repeat;
     wakeup.trigger_sec = trigger_sec;
     wakeup.day_offset = day_offset;
     wakeup.weekdays = weekdays;
     wakeup.callback = (void(*)(void*))callback;
     wakeup.arg = user_data;
     
     // Register with drtc for scheduled capture
     int result = rtc_register_wakeup_ex(&wakeup);
     if (result != 0) {
         LOG_SVC_ERROR("Failed to register RTC capture trigger: %d", result);
         return AICAM_ERROR;
     }
     
     LOG_SVC_INFO("RTC capture trigger registered: %s", name);
     return AICAM_OK;
 }
 
 aicam_result_t system_controller_unregister_trigger(system_controller_t *controller,
                                                    const char *name)
 {
     if (!controller || !controller->is_initialized || !name) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     // Unregister from drtc
     int result = rtc_unregister_task_by_name(name);
     if (result != 0) {
         LOG_SVC_ERROR("Failed to unregister capture trigger: %d", result);
         return AICAM_ERROR;
     }
     
     LOG_SVC_INFO("Capture trigger unregistered: %s", name);
     return AICAM_OK;
 }
 
 /* ==================== Simplified System Service Integration ==================== */
 
 
 /* ==================== U0 Module Integration ==================== */
 
 /**
  * @brief Process wakeup flag from U0 module
  * @param wakeup_flag Wakeup flag from U0 module
  * @return AICAM_OK on success
  */
 static aicam_result_t process_u0_wakeup_flag(uint32_t wakeup_flag)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     if (!(wakeup_flag & PWR_WAKEUP_FLAG_VALID)) {
         LOG_SVC_INFO("No valid wakeup flag, cold boot or power-on reset");
         return AICAM_OK;
     }
     
     LOG_SVC_INFO("Wakeup flag: 0x%08X", wakeup_flag);
     g_system_service_ctx.last_wakeup_flag = wakeup_flag;
     
     system_controller_t *controller = g_system_service_ctx.controller;
     
     // Check wakeup source and handle accordingly
     if (wakeup_flag & PWR_WAKEUP_FLAG_RTC_TIMING) {
         LOG_SVC_INFO("Woken by RTC timing");
         handle_wakeup_event(controller, WAKEUP_SOURCE_RTC);
     }
     else if (wakeup_flag & (PWR_WAKEUP_FLAG_RTC_ALARM_A | PWR_WAKEUP_FLAG_RTC_ALARM_B)) {
         LOG_SVC_INFO("Woken by RTC alarm");
         
         // Trigger scheduler check for RTC alarms
         if (wakeup_flag & PWR_WAKEUP_FLAG_RTC_ALARM_A) {
             LOG_SVC_INFO("RTC Alarm A triggered, checking scheduler 1");
             rtc_trigger_scheduler_check(1);
         }
         if (wakeup_flag & PWR_WAKEUP_FLAG_RTC_ALARM_B) {
             LOG_SVC_INFO("RTC Alarm B triggered, checking scheduler 2");
             rtc_trigger_scheduler_check(2);
         }
         
         handle_wakeup_event(controller, WAKEUP_SOURCE_RTC);
     }
    else if (wakeup_flag & PWR_WAKEUP_FLAG_WUFI) {
        LOG_SVC_INFO("Woken by WUFI");
        handle_wakeup_event(controller, WAKEUP_SOURCE_WUFI);
    }
    else if (wakeup_flag & PWR_WAKEUP_FLAG_KEY_MAX_PRESS) {
        // Super long press (10s) - Factory reset (highest priority)
        LOG_SVC_INFO("Woken by super long press (10s) - triggering factory reset");
        handle_wakeup_event(controller, WAKEUP_SOURCE_BUTTON_SUPER_LONG);
    }
    else if (wakeup_flag & PWR_WAKEUP_FLAG_KEY_LONG_PRESS) {
        // Long press (2s) - Enable AP
        LOG_SVC_INFO("Woken by long press (2s) - enabling AP");
        handle_wakeup_event(controller, WAKEUP_SOURCE_BUTTON_LONG);
    }
    else if (wakeup_flag & PWR_WAKEUP_FLAG_CONFIG_KEY) {
        // Short press - Take photo
        LOG_SVC_INFO("Woken by short press - taking photo");
        handle_wakeup_event(controller, WAKEUP_SOURCE_BUTTON);
    }
    else if (wakeup_flag & (PWR_WAKEUP_FLAG_PIR_HIGH | PWR_WAKEUP_FLAG_PIR_LOW | 
                       PWR_WAKEUP_FLAG_PIR_RISING | PWR_WAKEUP_FLAG_PIR_FALLING)) {
        LOG_SVC_INFO("Woken by PIR sensor");
        handle_wakeup_event(controller, WAKEUP_SOURCE_PIR);
    }
    else if (wakeup_flag & (PWR_WAKEUP_FLAG_SI91X | PWR_WAKEUP_FLAG_NET)) {
        LOG_SVC_INFO("Woken by network");
        handle_wakeup_event(controller, WAKEUP_SOURCE_REMOTE);
    }
     
     return AICAM_OK;
 }
 
 /**
  * @brief Configure U0 wakeup sources based on power mode and user configuration
  * @param controller System controller
  * @return Configured wakeup flags
  */
 static uint32_t configure_u0_wakeup_sources(system_controller_t *controller)
 {
     if (!controller) {
         return 0;
     }
     
     uint32_t wakeup_flags = 0;
     power_mode_t power_mode = controller->power_config.current_mode;
     
     // Low power mode: default only RTC, button, wifi wakeup
     if (power_mode == POWER_MODE_LOW_POWER) {
         // Default wakeup sources for low power mode
         wakeup_flags = PWR_WAKEUP_FLAG_RTC_TIMING | PWR_WAKEUP_FLAG_CONFIG_KEY;
         
         // Check user-configured wakeup sources
         if (controller->wakeup_sources[WAKEUP_SOURCE_PIR].enabled &&
             controller->wakeup_sources[WAKEUP_SOURCE_PIR].low_power_supported) {
             wakeup_flags |= PWR_WAKEUP_FLAG_PIR_RISING;
             LOG_SVC_INFO("PIR wakeup enabled in low power mode");
         }
         
         if (controller->wakeup_sources[WAKEUP_SOURCE_RTC].enabled) {
             wakeup_flags |= PWR_WAKEUP_FLAG_RTC_ALARM_A;
         }

         if (controller->work_config.remote_trigger.enable &&
             controller->wakeup_sources[WAKEUP_SOURCE_REMOTE].enabled &&
             controller->wakeup_sources[WAKEUP_SOURCE_REMOTE].low_power_supported) {
             
             //switch to si91x mqtt client
             mqtt_service_stop();
             mqtt_service_set_api_type(MQTT_API_TYPE_SI91X);
             aicam_result_t result = sl_net_netif_romote_wakeup_mode_ctrl(WAKEUP_MODE_WIFI);
             if (result != AICAM_OK) {
                 LOG_SVC_WARN("Failed to enable remote wakeup mode: %d", result);
                 return wakeup_flags;
             }
             result = mqtt_service_start();
             if (result != AICAM_OK) {
                 LOG_SVC_WARN("Failed to switch to si91x mqtt client: %d", result);
                 return wakeup_flags;
             }

             //enter low power mode
             result = sl_net_netif_low_power_mode_ctrl(1);
             if (result != AICAM_OK) {
                LOG_SVC_WARN("Failed to enable low power mode: %d", result);
                return wakeup_flags;
             }

             wakeup_flags |= PWR_WAKEUP_FLAG_SI91X;


         }
         
         LOG_SVC_INFO("Low power mode wakeup sources: 0x%08X", wakeup_flags);
     }
     // Full speed mode: support more wakeup sources
     else if (power_mode == POWER_MODE_FULL_SPEED) {
         // All wakeup sources available in full speed mode
         wakeup_flags = PWR_WAKEUP_FLAG_RTC_TIMING | 
                       PWR_WAKEUP_FLAG_RTC_ALARM_A |
                       PWR_WAKEUP_FLAG_CONFIG_KEY;
         
         if (controller->wakeup_sources[WAKEUP_SOURCE_PIR].enabled &&
             controller->wakeup_sources[WAKEUP_SOURCE_PIR].full_speed_supported) {
             wakeup_flags |= PWR_WAKEUP_FLAG_PIR_RISING;
         }
         
         if (controller->wakeup_sources[WAKEUP_SOURCE_REMOTE].enabled &&
             controller->wakeup_sources[WAKEUP_SOURCE_REMOTE].full_speed_supported) {
             wakeup_flags |= PWR_WAKEUP_FLAG_SI91X;
         }
         
         LOG_SVC_INFO("Full speed mode wakeup sources: 0x%08X", wakeup_flags);
     }
     
     return wakeup_flags;
 }
 
 /**
  * @brief Configure U0 power switches based on power mode
  * @param controller System controller
  * @return Configured power switch bits
  */
 static uint32_t configure_u0_power_switches(system_controller_t *controller)
 {
     if (!controller) {
         return 0;
     }
     
     uint32_t switch_bits = 0;
     power_mode_t power_mode = controller->power_config.current_mode;
     
     // Low power mode: turn off all power (Standby mode)
     if (power_mode == POWER_MODE_LOW_POWER) {
         // Check if PIR is enabled and needs 3V3 power
         if (controller->wakeup_sources[WAKEUP_SOURCE_PIR].enabled &&
             controller->wakeup_sources[WAKEUP_SOURCE_PIR].low_power_supported) {
             switch_bits |= PWR_3V3_SWITCH_BIT;
             LOG_SVC_INFO("Keeping 3V3 power for PIR in low power mode");
         }

         if (controller->wakeup_sources[WAKEUP_SOURCE_REMOTE].enabled &&
             controller->wakeup_sources[WAKEUP_SOURCE_REMOTE].low_power_supported &&
             controller->work_config.remote_trigger.enable) {
             switch_bits |= PWR_WIFI_SWITCH_BIT;
             switch_bits |= PWR_3V3_SWITCH_BIT;
             LOG_SVC_INFO("Keeping WiFi and 3V3 power for remote wakeup in low power mode");
         }
         // Otherwise all power off for maximum power saving
     }
     // Full speed mode: keep necessary power (Stop2 mode)
     else if (power_mode == POWER_MODE_FULL_SPEED) {
         // Keep essential power switches on
         switch_bits = PWR_3V3_SWITCH_BIT | PWR_AON_SWITCH_BIT | PWR_N6_SWITCH_BIT;
         
         // Keep WiFi on if remote wakeup is enabled
         if (controller->wakeup_sources[WAKEUP_SOURCE_REMOTE].enabled &&
             controller->wakeup_sources[WAKEUP_SOURCE_REMOTE].full_speed_supported) {
             switch_bits |= PWR_WIFI_SWITCH_BIT;
             LOG_SVC_INFO("Keeping WiFi power for remote wakeup");
         }
     }
     
     LOG_SVC_INFO("Power switches: 0x%08X", switch_bits);
     return switch_bits;
 }
 
 /**
  * @brief Prepare system for sleep mode
  * @return AICAM_OK on success
  */
 static aicam_result_t prepare_for_sleep(void)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     system_controller_t *controller = g_system_service_ctx.controller;
     
     LOG_SVC_INFO("Preparing system for sleep mode...");
     
     // Update RTC time to U0 chip before sleep
     int ret = u0_module_update_rtc_time();
     if (ret != 0) {
         LOG_SVC_ERROR("Failed to update RTC time to U0: %d", ret);
     }
     
     // Save critical configuration to NVS
     aicam_result_t result = system_service_save_config();
     if (result != AICAM_OK) {
         LOG_SVC_WARN("Failed to save config before sleep: %d", result);
     }
     
     // Set system state to sleep
     system_controller_set_state(controller, SYSTEM_STATE_SLEEP);
     
     return AICAM_OK;
 }
 
 /**
  * @brief Enter sleep mode based on current power mode configuration
  * @param sleep_duration_sec Sleep duration in seconds (0 = use timer trigger config)
  * @return AICAM_OK on success
  */
 static aicam_result_t enter_sleep_mode(uint32_t sleep_duration_sec)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     system_controller_t *controller = g_system_service_ctx.controller;
     
     // Prepare for sleep
     aicam_result_t result = prepare_for_sleep();
     if (result != AICAM_OK) {
         LOG_SVC_ERROR("Failed to prepare for sleep: %d", result);
         return result;
     }
     
     // Configure wakeup sources and power switches
     uint32_t wakeup_flags = configure_u0_wakeup_sources(controller);
     uint32_t switch_bits = configure_u0_power_switches(controller);
     
     // Determine sleep duration
     uint32_t sleep_sec = sleep_duration_sec;
     if (sleep_sec == 0) {
         // Use timer trigger interval if configured
         timer_trigger_config_t *timer_config = &controller->work_config.timer_trigger;
         if (timer_config->enable && timer_config->capture_mode == AICAM_TIMER_CAPTURE_MODE_INTERVAL) {
             sleep_sec = timer_config->interval_sec;
         }
     }
     
     // Get RTC alarm times from scheduler
     ms_bridging_alarm_t alarm_a = {0};
     ms_bridging_alarm_t alarm_b = {0};
     uint64_t next_wakeup_a = 0;
     uint64_t next_wakeup_b = 0;
     
     // Get next wakeup time for Alarm A (scheduler 1)
     if (rtc_get_next_wakeup_time(1, &next_wakeup_a) == 0) {
         // Convert timestamp to local time
         time_t wake_time = (time_t)next_wakeup_a;
         struct tm *tm_info = localtime(&wake_time);
         if (tm_info) {
             alarm_a.is_valid = 1;
             alarm_a.week_day = tm_info->tm_wday == 0 ? 7 : tm_info->tm_wday;
             alarm_a.date = 0;       // Not restricted by date
             alarm_a.hour = tm_info->tm_hour;
             alarm_a.minute = tm_info->tm_min;
             alarm_a.second = tm_info->tm_sec;
             wakeup_flags |= PWR_WAKEUP_FLAG_RTC_ALARM_A;
             LOG_SVC_INFO("RTC Alarm A configured: %02d:%02d:%02d, weekday=%d", 
                         alarm_a.hour, alarm_a.minute, alarm_a.second, alarm_a.week_day);
         }
     }
     
     // Get next wakeup time for Alarm B (scheduler 2)
     if (rtc_get_next_wakeup_time(2, &next_wakeup_b) == 0) {
         // Convert timestamp to local time
         time_t wake_time = (time_t)next_wakeup_b;
         struct tm *tm_info = localtime(&wake_time);
         if (tm_info) {
             alarm_b.is_valid = 1;
             alarm_b.week_day = tm_info->tm_wday == 0 ? 7 : tm_info->tm_wday;
             alarm_b.date = 0;       // Not restricted by date
             alarm_b.hour = tm_info->tm_hour;
             alarm_b.minute = tm_info->tm_min;
             alarm_b.second = tm_info->tm_sec;
             wakeup_flags |= PWR_WAKEUP_FLAG_RTC_ALARM_B;
             LOG_SVC_INFO("RTC Alarm B configured: %02d:%02d:%02d, weekday=%d", 
                         alarm_b.hour, alarm_b.minute, alarm_b.second, alarm_b.week_day);
         }
     }
     
     LOG_SVC_INFO("Entering sleep mode: wakeup=0x%08X, power=0x%08X, duration=%u", 
                  wakeup_flags, switch_bits, sleep_sec);
     
     // Enter sleep mode via U0 module with RTC alarms
     int ret = u0_module_enter_sleep_mode_ex(wakeup_flags, switch_bits, sleep_sec, 
                                             alarm_a.is_valid ? &alarm_a : NULL,
                                             alarm_b.is_valid ? &alarm_b : NULL);
     if (ret != 0) {
         LOG_SVC_ERROR("Failed to enter sleep mode: %d", ret);
         return AICAM_ERROR;
     }
     
     // Note: System will reset/wakeup after this point
     // Execution will continue from system startup
     
     return AICAM_OK;
 }
 
 /**
  * @brief Check if system should enter sleep after task completion
  * @return AICAM_TRUE if should sleep, AICAM_FALSE otherwise
  */
 static aicam_bool_t should_enter_sleep_after_task(void)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_FALSE;
     }
     
     system_controller_t *controller = g_system_service_ctx.controller;
     power_mode_t power_mode = controller->power_config.current_mode;
     
     // Low power mode: enter sleep after task completion
     if (power_mode == POWER_MODE_LOW_POWER) {
         LOG_SVC_INFO("Low power mode: will enter sleep after task completion");
         return AICAM_TRUE;
     }
     
     // Full speed mode: do not enter sleep, keep running
     LOG_SVC_INFO("Full speed mode: remain active after task completion");
     return AICAM_FALSE;
 }
 
 aicam_result_t system_service_init(void* config)
 {
     if (g_system_service_ctx.is_initialized) {
         return AICAM_OK; // Already initialized
     }
     
     LOG_SVC_INFO("Initializing simplified system service...");
     
     // Create system controller
     g_system_service_ctx.controller = system_controller_create();
     if (!g_system_service_ctx.controller) {
         LOG_SVC_ERROR("Failed to create system controller");
         return AICAM_ERROR_NO_MEMORY;
     }
     
     // Initialize system controller
     aicam_result_t result = system_controller_init(g_system_service_ctx.controller);
     if (result != AICAM_OK) {
         LOG_SVC_ERROR("Failed to initialize system controller");
         system_controller_destroy(g_system_service_ctx.controller);
         memset(&g_system_service_ctx, 0, sizeof(g_system_service_ctx));
         return result;
     }
     
     // Load work mode configuration from json_config_mgr
     work_mode_config_t work_config;
     result = json_config_get_work_mode_config(&work_config);
     if (result == AICAM_OK) {
         // Set the configuration without triggering save (to avoid redundant NVS writes)
         g_system_service_ctx.controller->work_config = work_config;
         g_system_service_ctx.controller->current_work_mode = work_config.work_mode;
         LOG_SVC_INFO("Work mode configuration loaded from NVS: mode=%d", work_config.work_mode);
         
     } else {
         LOG_SVC_WARN("Failed to load work mode config from NVS, using defaults: %d", result);
         // Initialize with default work mode configuration
         memset(&g_system_service_ctx.controller->work_config, 0, sizeof(work_mode_config_t));
         g_system_service_ctx.controller->work_config.work_mode = AICAM_WORK_MODE_IMAGE;
         g_system_service_ctx.controller->current_work_mode = AICAM_WORK_MODE_IMAGE;
     }
     
     // Initialize U0 module integration
     g_system_service_ctx.last_wakeup_flag = 0;
     g_system_service_ctx.task_completed = false;
     g_system_service_ctx.sleep_pending = false;
     
     // Sync RTC time from U0 on startup
     int ret = u0_module_sync_rtc_time();
     if (ret == 0) {
         LOG_SVC_INFO("RTC time synchronized from U0");
     } else {
         LOG_SVC_WARN("Failed to sync RTC time from U0: %d", ret);
     }
     
     // Check and store wakeup flag from U0 (but don't process yet)
     uint32_t wakeup_flag = 0;
     ret = u0_module_get_wakeup_flag(&wakeup_flag);
     if (ret == 0) {
         LOG_SVC_INFO("System woken by U0, wakeup flag: 0x%08X (stored for later processing)", wakeup_flag);
         g_system_service_ctx.last_wakeup_flag = wakeup_flag;
     } else {
         LOG_SVC_WARN("Failed to get wakeup flag from U0: %d", ret);
         g_system_service_ctx.last_wakeup_flag = 0;
     }
     
     g_system_service_ctx.is_initialized = true;
     g_system_service_ctx.is_started = false;
     g_system_service_ctx.timer_trigger_configured = false;
     g_system_service_ctx.timer_trigger_active = false;
     
     LOG_SVC_INFO("Simplified system service initialized successfully");
     return AICAM_OK;
 }
 
 aicam_result_t system_service_deinit(void)
 {
     if (!g_system_service_ctx.is_initialized) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     LOG_SVC_INFO("Deinitializing simplified system service...");
     
     // Stop service if still running
     if (g_system_service_ctx.is_started) {
         aicam_result_t result = system_service_stop();
         if (result != AICAM_OK) {
             LOG_SVC_ERROR("Failed to stop system service: %d", result);
             return result;
         }
     }
     
     if (g_system_service_ctx.controller) {
         system_controller_deinit(g_system_service_ctx.controller);
         system_controller_destroy(g_system_service_ctx.controller);
     }
     
     memset(&g_system_service_ctx, 0, sizeof(g_system_service_ctx));
     
     LOG_SVC_INFO("Simplified system service deinitialized");

     return AICAM_OK;
 }
 
 system_service_context_t* system_service_get_context(void)
 {
     if (!g_system_service_ctx.is_initialized) {
         return NULL;
     }
     return &g_system_service_ctx;
 }
 
 aicam_result_t system_service_get_status(void)
 {
     if (!g_system_service_ctx.is_initialized) {
         return AICAM_ERROR;
     }
     
     // Check if system controller is properly initialized
     if (g_system_service_ctx.controller) {
         return AICAM_OK;
     }
     
     return AICAM_ERROR;
 }
 
 system_controller_t* system_service_get_controller(void)
 {
     if (!g_system_service_ctx.is_initialized) {
         return NULL;
     }
     
     return g_system_service_ctx.controller;
 }
 
 /* ==================== System Service Start/Stop Implementation ==================== */
 
 aicam_result_t system_service_start(void)
 {
     if (!g_system_service_ctx.is_initialized) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     if (g_system_service_ctx.is_started) {
         LOG_SVC_INFO("System service already started");
         return AICAM_OK;
     }
     
     LOG_SVC_INFO("Starting system service...");
     
     system_controller_t *controller = g_system_service_ctx.controller;
     if (!controller) {
         LOG_SVC_ERROR("System controller not available");
         return AICAM_ERROR_UNAVAILABLE;
     }
     
    // Set system state to running
    aicam_result_t result = system_controller_set_state(controller, SYSTEM_STATE_ACTIVE);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set system state to running: %d", result);
        return result;
    }
    
    // Register default capture callback if none registered
    if (!controller->capture_callback) {
        LOG_SVC_INFO("No capture callback registered, using default framework");
        controller->capture_callback = default_capture_callback;
        controller->capture_callback_user_data = controller;
    }
    
    // Apply timer trigger configuration if enabled
     timer_trigger_config_t *timer_config = &controller->work_config.timer_trigger;
     if (timer_config->enable) {
         result = apply_timer_trigger_config(controller, timer_config);
         if (result == AICAM_OK) {
             g_system_service_ctx.timer_trigger_configured = true;
             g_system_service_ctx.timer_trigger_active = true;
             LOG_SVC_INFO("Timer trigger configuration applied successfully");
         } else {
             LOG_SVC_ERROR("Failed to apply timer trigger configuration: %d", result);
             g_system_service_ctx.timer_trigger_configured = false;
             g_system_service_ctx.timer_trigger_active = false;
         }
     } else {
         LOG_SVC_INFO("Timer trigger is disabled");
         g_system_service_ctx.timer_trigger_configured = true;
         g_system_service_ctx.timer_trigger_active = false;
     }
     
     g_system_service_ctx.is_started = true;
     
     LOG_SVC_INFO("System service started successfully");
     return AICAM_OK;
 }
 
 aicam_result_t system_service_stop(void)
 {
     if (!g_system_service_ctx.is_initialized) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     if (!g_system_service_ctx.is_started) {
         LOG_SVC_INFO("System service already stopped");
         return AICAM_OK;
     }
     
     LOG_SVC_INFO("Stopping system service...");
     
     system_controller_t *controller = g_system_service_ctx.controller;
     if (controller) {
         // Stop timer trigger if active
         if (g_system_service_ctx.timer_trigger_active && strlen(controller->timer_task_name) > 0) {
             rtc_unregister_task_by_name(controller->timer_task_name);
             controller->timer_trigger_active = AICAM_FALSE;
             g_system_service_ctx.timer_trigger_active = false;
             LOG_SVC_INFO("Timer trigger stopped");
         }
         
         // Set system state to stopped
         system_controller_set_state(controller, SYSTEM_STATE_SHUTDOWN);
     }
     
     g_system_service_ctx.is_started = false;
     g_system_service_ctx.timer_trigger_configured = false;
     g_system_service_ctx.timer_trigger_active = false;
     
     LOG_SVC_INFO("System service stopped successfully");
     return AICAM_OK;
 }
 
 /* ==================== Timer Trigger Public API ==================== */
 
 /**
  * @brief Start timer trigger with current configuration
  * @return AICAM_OK on success
  */
 aicam_result_t system_service_start_timer_trigger(void)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     if (!g_system_service_ctx.is_started) {
         LOG_SVC_WARN("System service not started, cannot start timer trigger");
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     system_controller_t *controller = g_system_service_ctx.controller;
     
     timer_trigger_config_t *timer_config = &controller->work_config.timer_trigger;
     if (!timer_config->enable) {
         LOG_SVC_WARN("Timer trigger is disabled in configuration");
         return AICAM_ERROR_NOT_SUPPORTED;
     }
     
     aicam_result_t result = apply_timer_trigger_config(controller, timer_config);
     if (result == AICAM_OK) {
         g_system_service_ctx.timer_trigger_configured = true;
         g_system_service_ctx.timer_trigger_active = true;
         LOG_SVC_INFO("Timer trigger started successfully");
     } else {
         g_system_service_ctx.timer_trigger_configured = false;
         g_system_service_ctx.timer_trigger_active = false;
         LOG_SVC_ERROR("Failed to start timer trigger: %d", result);
     }
     
     return result;
 }
 
 /**
  * @brief Stop timer trigger
  * @return AICAM_OK on success
  */
 aicam_result_t system_service_stop_timer_trigger(void)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     system_controller_t *controller = g_system_service_ctx.controller;
     
     if (g_system_service_ctx.timer_trigger_active && strlen(controller->timer_task_name) > 0) {
         rtc_unregister_task_by_name(controller->timer_task_name);
         controller->timer_trigger_active = AICAM_FALSE;
         g_system_service_ctx.timer_trigger_active = false;
         LOG_SVC_INFO("Timer trigger stopped manually");
         return AICAM_OK;
     }
     
     LOG_SVC_WARN("Timer trigger is not active");
     return AICAM_ERROR_UNAVAILABLE;
 }
 
 /**
  * @brief Get timer trigger status
  * @param active Pointer to store active status
  * @param task_count Pointer to store task execution count
  * @return AICAM_OK on success
  */
 aicam_result_t system_service_get_timer_trigger_status(aicam_bool_t *active, uint32_t *task_count)
 {
     if (!active || !task_count) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     system_controller_t *controller = g_system_service_ctx.controller;
     
     *active = g_system_service_ctx.timer_trigger_active;
     *task_count = controller->timer_task_count;
     
     return AICAM_OK;
 }
 
 /**
  * @brief Apply timer trigger configuration changes
  * @return AICAM_OK on success
  */
 aicam_result_t system_service_apply_timer_trigger_config(void)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     if (!g_system_service_ctx.is_started) {
         LOG_SVC_WARN("System service not started, cannot apply timer trigger configuration");
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     system_controller_t *controller = g_system_service_ctx.controller;
     
     timer_trigger_config_t *timer_config = &controller->work_config.timer_trigger;
     
     // Stop existing timer trigger if active
     if (g_system_service_ctx.timer_trigger_active && strlen(controller->timer_task_name) > 0) {
         rtc_unregister_task_by_name(controller->timer_task_name);
         controller->timer_trigger_active = AICAM_FALSE;
         g_system_service_ctx.timer_trigger_active = false;
         LOG_SVC_INFO("Stopped existing timer trigger for reconfiguration");
     }
     
     // Apply new configuration if enabled
     if (timer_config->enable) {
         aicam_result_t result = apply_timer_trigger_config(controller, timer_config);
         if (result == AICAM_OK) {
             g_system_service_ctx.timer_trigger_configured = true;
             g_system_service_ctx.timer_trigger_active = true;
             LOG_SVC_INFO("Timer trigger configuration applied successfully");
         } else {
             g_system_service_ctx.timer_trigger_configured = false;
             g_system_service_ctx.timer_trigger_active = false;
             LOG_SVC_ERROR("Failed to apply timer trigger configuration: %d", result);
             return result;
         }
     } else {
         LOG_SVC_INFO("Timer trigger is disabled, configuration applied");
         g_system_service_ctx.timer_trigger_configured = true;
         g_system_service_ctx.timer_trigger_active = false;
     }
     
     return AICAM_OK;
 }
 
 /**
  * @brief Get system service status
  * @param is_started Pointer to store started status
  * @param timer_configured Pointer to store timer configuration status
  * @param timer_active Pointer to store timer active status
  * @return AICAM_OK on success
  */
 aicam_result_t system_service_get_status_info(aicam_bool_t *is_started, 
                                             aicam_bool_t *timer_configured, 
                                             aicam_bool_t *timer_active)
 {
     if (!is_started || !timer_configured || !timer_active) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     if (!g_system_service_ctx.is_initialized) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     *is_started = g_system_service_ctx.is_started;
     *timer_configured = g_system_service_ctx.timer_trigger_configured;
     *timer_active = g_system_service_ctx.timer_trigger_active;
     
     return AICAM_OK;
 }
 
 /* ==================== Wakeup Source Management Public API ==================== */
 


/**
 * @brief Get wakeup source type
 * @return Wakeup source type
 */
wakeup_source_type_t system_service_get_wakeup_source_type(void)
{
   uint32_t wakeup_flag = u0_module_get_wakeup_flag_ex();
   
   if (wakeup_flag & PWR_WAKEUP_FLAG_RTC_TIMING || wakeup_flag & PWR_WAKEUP_FLAG_RTC_ALARM_A || wakeup_flag & PWR_WAKEUP_FLAG_RTC_ALARM_B) {
       return WAKEUP_SOURCE_RTC;
   } else if (wakeup_flag & PWR_WAKEUP_FLAG_KEY_MAX_PRESS) {
       // Super long press (10s) - highest priority
       return WAKEUP_SOURCE_BUTTON_SUPER_LONG;
   } else if (wakeup_flag & PWR_WAKEUP_FLAG_KEY_LONG_PRESS) {
       // Long press (2s)
       return WAKEUP_SOURCE_BUTTON_LONG;
   } else if (wakeup_flag & PWR_WAKEUP_FLAG_CONFIG_KEY) {
       // Short press
       return WAKEUP_SOURCE_BUTTON;
   } else if (wakeup_flag & PWR_WAKEUP_FLAG_PIR_HIGH || wakeup_flag & PWR_WAKEUP_FLAG_PIR_LOW || wakeup_flag & PWR_WAKEUP_FLAG_PIR_RISING || wakeup_flag & PWR_WAKEUP_FLAG_PIR_FALLING) {
       return WAKEUP_SOURCE_PIR;
   } else if( wakeup_flag & PWR_WAKEUP_FLAG_VALID) {
       return WAKEUP_SOURCE_OTHER;
   }

   return WAKEUP_SOURCE_OTHER;
}
 
 /**
  * @brief Configure wakeup source
  * @param source Wakeup source type
  * @param config Wakeup source configuration
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_configure_wakeup_source(wakeup_source_type_t source, const wakeup_source_config_t *config)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     if (source >= WAKEUP_SOURCE_MAX || !config) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     system_controller_t *controller = g_system_service_ctx.controller;
     controller->wakeup_sources[source] = *config;
     
     LOG_SVC_INFO("Wakeup source %d configured: enabled=%d, low_power=%d, full_speed=%d", 
                  source, config->enabled, config->low_power_supported, config->full_speed_supported);
     
     return AICAM_OK;
 }
 
 /**
  * @brief Get wakeup source configuration
  * @param source Wakeup source type
  * @param config Pointer to store wakeup source configuration
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_get_wakeup_source_config(wakeup_source_type_t source, wakeup_source_config_t *config)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     if (source >= WAKEUP_SOURCE_MAX || !config) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     system_controller_t *controller = g_system_service_ctx.controller;
     *config = controller->wakeup_sources[source];
     
     return AICAM_OK;
 }
 
 /**
  * @brief Check if wakeup source is supported in current power mode
  * @param source Wakeup source type
  * @return AICAM_TRUE if supported, AICAM_FALSE otherwise
  */
 aicam_bool_t system_service_is_wakeup_source_supported(wakeup_source_type_t source)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_FALSE;
     }
     
     return is_wakeup_source_supported(g_system_service_ctx.controller, source);
 }
 
 /**
  * @brief Handle wakeup event from external source
  * @param source Wakeup source type
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_handle_wakeup_event(wakeup_source_type_t source)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     if (source >= WAKEUP_SOURCE_MAX) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     // Check if wakeup source is supported in current power mode
     if (!is_wakeup_source_supported(g_system_service_ctx.controller, source)) {
         LOG_SVC_WARN("Wakeup source %d not supported in current power mode", source);
         return AICAM_ERROR_NOT_SUPPORTED;
     }
     
     return handle_wakeup_event(g_system_service_ctx.controller, source);
 }
 
 /**
  * @brief Update system activity (for power management)
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_update_activity(void)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     update_activity(g_system_service_ctx.controller);
     return AICAM_OK;
 }
 
 /**
  * @brief Get system activity counter
  * @param counter Pointer to store activity counter
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_get_activity_counter(uint32_t *counter)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     if (!counter) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     *counter = g_system_service_ctx.controller->activity_counter;
     return AICAM_OK;
 }
 
 /**
  * @brief Force save configuration to persistent storage
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_save_config(void)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     // Save work mode configuration using existing helper function
     aicam_result_t result = save_work_mode_config_to_nvs(g_system_service_ctx.controller);
     if (result != AICAM_OK) {
         LOG_SVC_ERROR("Failed to save work mode config: %d", result);
         return result;
     }
     
     LOG_SVC_INFO("System service configuration saved successfully");
     return AICAM_OK;
 }
 
 /**
  * @brief Force load configuration from persistent storage
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_load_config(void)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     // Load work mode configuration
     work_mode_config_t work_config;
     aicam_result_t result = json_config_get_work_mode_config(&work_config);
     if (result == AICAM_OK) {
         result = system_controller_set_work_config(g_system_service_ctx.controller, &work_config);
         if (result != AICAM_OK) {
             LOG_SVC_ERROR("Failed to load work mode config: %d", result);
             return result;
         }
     } else {
         LOG_SVC_ERROR("Failed to get work mode config from storage: %d", result);
         return result;
     }
     
     LOG_SVC_INFO("System service configuration loaded successfully");
     return AICAM_OK;
 }
 
 /* ==================== Power Mode Configuration API Implementation ==================== */
 
 /**
  * @brief Get power mode configuration
  * @param config Pointer to store power mode configuration
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_get_power_mode_config(power_mode_config_t *config)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     if (!config) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     *config = g_system_service_ctx.controller->power_config;
     return AICAM_OK;
 }
 
 /**
  * @brief Set power mode configuration
  * @param config Power mode configuration to set
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_set_power_mode_config(const power_mode_config_t *config)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     if (!config) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     // Validate configuration
     if (config->current_mode >= POWER_MODE_MAX || config->default_mode >= POWER_MODE_MAX) {
         LOG_SVC_ERROR("Invalid power mode values: current=%u, default=%u", 
                       config->current_mode, config->default_mode);
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     // Update configuration
     g_system_service_ctx.controller->power_config = *config;
     
     // Save to NVS
     aicam_result_t result = json_config_set_power_mode_config(config);
     if (result != AICAM_OK) {
         LOG_SVC_ERROR("Failed to save power mode configuration to NVS: %d", result);
         return result;
     }
     
     LOG_SVC_INFO("Power mode configuration updated: current=%u, default=%u, timeout=%u", 
                  config->current_mode, config->default_mode, config->low_power_timeout_ms);
     
     return AICAM_OK;
 }
 
 /**
  * @brief Get current power mode
  * @return Current power mode (POWER_MODE_LOW_POWER or POWER_MODE_FULL_SPEED)
  */
 power_mode_t system_service_get_current_power_mode(void)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return POWER_MODE_LOW_POWER; // Default to low power mode
     }
     
     return g_system_service_ctx.controller->power_config.current_mode;
 }
 
 /**
  * @brief Set current power mode
  * @param mode Power mode to set
  * @param trigger_type Trigger type for this change
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_set_current_power_mode(power_mode_t mode, power_trigger_type_t trigger_type)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     if (mode >= POWER_MODE_MAX) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     return system_controller_set_power_mode(g_system_service_ctx.controller, mode, trigger_type);
 }
 
 /* ==================== Sleep Management Public API ==================== */
 
 /**
  * @brief Mark task as completed and check if should enter sleep
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_task_completed(void)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     g_system_service_ctx.task_completed = true;
     LOG_SVC_INFO("Task marked as completed");
     
     // Check if should enter sleep after task
     if (should_enter_sleep_after_task()) {
         g_system_service_ctx.sleep_pending = true;
         LOG_SVC_INFO("Sleep pending after task completion");
     }
     
     return AICAM_OK;
 }
 
 /**
  * @brief Enter sleep mode immediately
  * @param sleep_duration_sec Sleep duration in seconds (0 = use timer config)
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_enter_sleep(uint32_t sleep_duration_sec)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     LOG_SVC_INFO("Entering sleep mode with duration: %u seconds", sleep_duration_sec);
     return enter_sleep_mode(sleep_duration_sec);
 }
 
 /**
  * @brief Check if sleep is pending
  * @param pending Pointer to store pending status
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_is_sleep_pending(aicam_bool_t *pending)
 {
     if (!pending) {
         LOG_SVC_ERROR("Pending pointer is NULL");
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     if (!g_system_service_ctx.is_initialized) {
         LOG_SVC_ERROR("System service not initialized");
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     // mutex lock
     *pending = g_system_service_ctx.sleep_pending;
     return AICAM_OK;
 }
 
 /**
  * @brief Execute pending sleep if applicable
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_execute_pending_sleep(void)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     if (!g_system_service_ctx.sleep_pending) {
         return AICAM_OK; // No pending sleep
     }
     
     LOG_SVC_INFO("Executing pending sleep operation");
     g_system_service_ctx.sleep_pending = false;
     
     return enter_sleep_mode(0);
 }
 
 /**
  * @brief Get last wakeup flag from U0 module
  * @param wakeup_flag Pointer to store wakeup flag
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_get_last_wakeup_flag(uint32_t *wakeup_flag)
 {
     if (!wakeup_flag) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     
     if (!g_system_service_ctx.is_initialized) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     *wakeup_flag = g_system_service_ctx.last_wakeup_flag;
     return AICAM_OK;
 }
 
 /**
  * @brief Force update RTC time to U0 module
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_update_rtc_to_u0(void)
 {
     int ret = u0_module_update_rtc_time();
     if (ret != 0) {
         LOG_SVC_ERROR("Failed to update RTC time to U0: %d", ret);
         return AICAM_ERROR;
     }
     
     LOG_SVC_INFO("RTC time updated to U0 successfully");
     return AICAM_OK;
 }
 
 /**
  * @brief Process stored wakeup event (call this after all services are started)
  * @details This should be called by application after all services are initialized
  *          and started to handle wakeup events that triggered system boot
  * @return AICAM_OK on success, error code otherwise
  */
 aicam_result_t system_service_process_wakeup_event(void)
 {
     if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     
     uint32_t wakeup_flag = g_system_service_ctx.last_wakeup_flag;
     
     if (wakeup_flag == 0) {
         LOG_SVC_INFO("No wakeup flag to process (cold boot or no wakeup event)");
         return AICAM_OK;
     }
     
     LOG_SVC_INFO("Processing stored wakeup event: 0x%08X", wakeup_flag);
     
     // Process the wakeup event now that all services are ready
     aicam_result_t result = process_u0_wakeup_flag(wakeup_flag);
     if (result != AICAM_OK) {
         LOG_SVC_ERROR("Failed to process wakeup event: %d", result);
         return result;
     }
     
    LOG_SVC_INFO("Wakeup event processed successfully");
    return AICAM_OK;
}

/* ==================== Unified Capture Entry ==================== */

aicam_result_t system_service_capture_request(const system_capture_request_t *request,
                                              system_capture_response_t *response)
{
    system_capture_request_t resolved = g_capture_defaults;
    if (request) {
        resolved.enable_ai = request->enable_ai;
        resolved.store_to_sd = request->store_to_sd;
        resolved.fast_fail_mqtt = request->fast_fail_mqtt;
        if (request->chunk_size > 0) {
            resolved.chunk_size = request->chunk_size;
        }
    }

    if (g_capture_in_progress) {
        return AICAM_ERROR_BUSY;
    }

    g_capture_in_progress = AICAM_TRUE;
    g_fast_fail_mqtt_policy = resolved.fast_fail_mqtt;

    uint64_t start_ms = rtc_get_uptime_ms();
    aicam_result_t ret = system_service_capture_and_upload_mqtt(
        resolved.enable_ai,
        resolved.chunk_size,
        resolved.store_to_sd);
    uint64_t duration_ms = rtc_get_uptime_ms() - start_ms;

    g_capture_in_progress = AICAM_FALSE;
    g_fast_fail_mqtt_policy = AICAM_FALSE;

    if (response) {
        response->result = ret;
        response->duration_ms = duration_ms;
    }

    return ret;
}

/* ==================== Image Capture and Upload API Implementation ==================== */

/**
 * @brief Capture image with AI inference and upload to MQTT
 * @param enable_ai Enable AI inference (AICAM_TRUE/AICAM_FALSE)
 * @param chunk_size Chunk size for large images (0 = auto: 10KB)
 * @param qos MQTT QoS level (0, 1, or 2)
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_capture_and_upload_mqtt(aicam_bool_t enable_ai, 
                                                     uint32_t chunk_size,
                                                     aicam_bool_t store_to_sd)
{
    if (!g_system_service_ctx.is_initialized || !g_system_service_ctx.controller) {
        LOG_SVC_ERROR("System service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    // Record start time for total duration calculation (use uptime for accurate timing)
    uint64_t total_start_time = rtc_get_uptime_ms();
    uint64_t step_start_time, step_end_time, step_duration;

    LOG_SVC_INFO("========== Starting image capture and MQTT upload (AI: %s) ==========", 
                 enable_ai ? "enabled" : "disabled");

    // Check if this is an RTC wakeup (for fast capture)
    wakeup_source_type_t wakeup_source = system_service_get_wakeup_source_type();
    aicam_bool_t is_rtc_wakeup = (wakeup_source == WAKEUP_SOURCE_RTC);
    aicam_bool_t is_button_wakeup = (wakeup_source == WAKEUP_SOURCE_BUTTON);

    // Step 1: Capture image with optional AI inference
    uint8_t *jpeg_buffer = NULL;
    int jpeg_size = 0;
    nn_result_t nn_result = {0};
    aicam_result_t ret = AICAM_OK;

    step_start_time = rtc_get_uptime_ms();
    if (is_rtc_wakeup || is_button_wakeup) {
        LOG_SVC_INFO("[TIMING] Step 1: Capturing image using fast capture API (RTC wakeup)...");
        ret = device_service_camera_capture_fast(&jpeg_buffer, &jpeg_size, enable_ai, &nn_result);
        step_end_time = rtc_get_uptime_ms();
        step_duration = step_end_time - step_start_time;
        
        if (ret != AICAM_OK) {
            LOG_SVC_ERROR("[TIMING] Step 1 FAILED (fast capture): %d (duration: %lu ms)", ret, (unsigned long)step_duration);
            return ret;
        }
        LOG_SVC_INFO("[TIMING] Step 1 COMPLETED (fast capture): Image captured - %u bytes (duration: %lu ms)", 
                     jpeg_size, (unsigned long)step_duration);
    } else {
        LOG_SVC_INFO("[TIMING] Step 1: Capturing image...");
        ret = device_service_camera_capture(&jpeg_buffer, &jpeg_size, enable_ai, &nn_result);
        step_end_time = rtc_get_uptime_ms();
        step_duration = step_end_time - step_start_time;
        
        if (ret != AICAM_OK) {
            LOG_SVC_ERROR("[TIMING] Step 1 FAILED: %d (duration: %lu ms)", ret, (unsigned long)step_duration);
            return ret;
        }
        LOG_SVC_INFO("[TIMING] Step 1 COMPLETED: Image captured - %u bytes (duration: %lu ms)", 
                     jpeg_size, (unsigned long)step_duration);
    }

    //store image to sd card if sd card is connected
    if(store_to_sd && device_service_storage_is_sd_connected()){
        step_start_time = rtc_get_uptime_ms();
        LOG_SVC_INFO("[TIMING] Step 1.1: Storing image to SD card...");
        char filename[64];
        snprintf(filename, sizeof(filename), "image_%u.jpg", (unsigned int)rtc_get_timeStamp());
        ret = sd_write_file(jpeg_buffer, jpeg_size, filename);
        step_end_time = rtc_get_uptime_ms();
        step_duration = step_end_time - step_start_time;
        
        if(ret != AICAM_OK){
            LOG_SVC_ERROR("[TIMING] Step 1.1 FAILED: Store image to sd card failed: %d (duration: %lu ms)", 
                         ret, (unsigned long)step_duration);
        } else {
            LOG_SVC_INFO("[TIMING] Step 1.1 COMPLETED: Image stored to SD card (duration: %lu ms)", 
                        (unsigned long)step_duration);
        }
    }

    // Validate capture result
    if (!jpeg_buffer) {
        LOG_SVC_ERROR("[TIMING] Validation FAILED: jpeg_buffer is NULL");
        return AICAM_ERROR;
    }
    if (jpeg_size == 0) {
        LOG_SVC_ERROR("[TIMING] Validation FAILED: jpeg_size is 0");
        device_service_camera_free_jpeg_buffer(jpeg_buffer);
        return AICAM_ERROR;
    }

    // Step 2: Prepare metadata
    step_start_time = rtc_get_uptime_ms();
    LOG_SVC_INFO("[TIMING] Step 2: Preparing metadata...");
    jpegc_params_t jpeg_enc_param = {0};
    ret = device_service_camera_get_jpeg_params(&jpeg_enc_param);
    if (ret != AICAM_OK) {
        LOG_SVC_ERROR("[TIMING] Step 2 FAILED: Failed to get jpeg parameters: %d", ret);
        device_service_camera_free_jpeg_buffer(jpeg_buffer);
        return ret;
    }

    mqtt_image_metadata_t metadata = {0};
    mqtt_service_generate_image_id(metadata.image_id, "cam01");
    metadata.timestamp = rtc_get_timeStamp();
    metadata.format = MQTT_IMAGE_FORMAT_JPEG;
    metadata.width = jpeg_enc_param.ImageWidth;
    metadata.height = jpeg_enc_param.ImageHeight;
    metadata.size = (uint32_t)jpeg_size;
    metadata.quality = jpeg_enc_param.ImageQuality;
    step_end_time = rtc_get_uptime_ms();
    step_duration = step_end_time - step_start_time;
    LOG_SVC_INFO("[TIMING] Step 2 COMPLETED: Metadata prepared (duration: %lu ms)", 
                 (unsigned long)step_duration);

    // Step 3: Prepare AI result (if enabled and valid)
    step_start_time = rtc_get_uptime_ms();
    LOG_SVC_INFO("[TIMING] Step 3: Preparing AI result...");
    mqtt_ai_result_t mqtt_ai_result = {0};
    mqtt_ai_result_t *ai_result_ptr = NULL;
    
    if (enable_ai && nn_result.is_valid) {
        nn_model_info_t model_info = {0};
        aicam_result_t ai_info_ret = ai_service_get_model_info(&model_info);
        if (ai_info_ret != AICAM_OK) {
            LOG_SVC_ERROR("[TIMING] Step 3 WARNING: Failed to get AI model info: %d, using defaults", ai_info_ret);
            // Use default values if model info retrieval fails
            strncpy(model_info.name, "unknown", sizeof(model_info.name) - 1);
            strncpy(model_info.version, "1.0", sizeof(model_info.version) - 1);
        }
        
        LOG_SVC_INFO("[TIMING] Step 3: Initializing AI result (model: %s, version: %s)...", 
                     model_info.name, model_info.version);
        aicam_result_t ai_result_ret = mqtt_service_init_ai_result(&mqtt_ai_result, &nn_result, 
                                                                    model_info.name, model_info.version, 50);
        if (ai_result_ret != AICAM_OK) {
            LOG_SVC_ERROR("[TIMING] Step 3 WARNING: Failed to init AI result: %d, continuing without AI result", 
                         ai_result_ret);
            ai_result_ptr = NULL;
        } else {
            ai_result_ptr = &mqtt_ai_result;
        }
        step_end_time = rtc_get_uptime_ms();
        step_duration = step_end_time - step_start_time;
        LOG_SVC_INFO("[TIMING] Step 3 COMPLETED: AI inference result %s (duration: %lu ms)", 
                     ai_result_ptr ? "included" : "failed", (unsigned long)step_duration);
    } else {
        step_end_time = rtc_get_uptime_ms();
        step_duration = step_end_time - step_start_time;
        LOG_SVC_INFO("[TIMING] Step 3 COMPLETED: AI result skipped (duration: %lu ms)", 
                     (unsigned long)step_duration);
    }

    step_start_time = rtc_get_uptime_ms();
    LOG_SVC_INFO("[TIMING] Step 3.1: Checking MQTT network connection...");
    uint32_t current_flags = service_get_ready_flags();

    if (g_fast_fail_mqtt_policy) {
        if (!mqtt_service_is_connected()) {
            LOG_SVC_ERROR("[TIMING] Step 3.1 FAST-FAIL: MQTT not connected (flags=0x%08X)", current_flags);
            device_service_camera_free_jpeg_buffer(jpeg_buffer);
            return AICAM_ERROR_UNAVAILABLE;
        }
    } else {
        LOG_SVC_INFO("[TIMING] Step 3.1: Current service flags: 0x%08X, MQTT_NET_CONNECTED: %s", 
                     current_flags, (current_flags & MQTT_NET_CONNECTED) ? "YES" : "NO");
        aicam_result_t result = service_wait_for_ready(MQTT_NET_CONNECTED, AICAM_TRUE, 15000);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("[TIMING] Step 3.1 FAILED: Failed to wait for MQTT network connected: %d (timeout: 15s)", result);
            LOG_SVC_ERROR("[TIMING] Step 3.1: Final service flags: 0x%08X", service_get_ready_flags());
            device_service_camera_free_jpeg_buffer(jpeg_buffer);
            return AICAM_ERROR_TIMEOUT;
        }
    }

    step_end_time = rtc_get_uptime_ms();
    step_duration = step_end_time - step_start_time;
    LOG_SVC_INFO("[TIMING] Step 3.1 COMPLETED: MQTT network ready (duration: %lu ms)", 
                 (unsigned long)step_duration);

    // Step 4: Check MQTT connection and upload
    step_start_time = rtc_get_uptime_ms();
    LOG_SVC_INFO("[TIMING] Step 4: Checking MQTT connection and uploading...");
    aicam_result_t upload_result = AICAM_ERROR;
    
    if (mqtt_service_is_connected()) {
        LOG_SVC_INFO("[TIMING] MQTT connected - uploading image");
        
        // Determine upload method based on image size
        const uint32_t size_threshold = 1024 * 1024; // 1MB
        int mqtt_result;
        uint64_t upload_start_time = rtc_get_uptime_ms();
        
        if (jpeg_size < size_threshold) {
            // Small image - single upload
            LOG_SVC_INFO("[TIMING] Using single upload (size: %u bytes)", jpeg_size);
            mqtt_result = mqtt_service_publish_image_with_ai(
                NULL, // Use default topic
                jpeg_buffer,
                jpeg_size,
                &metadata,
                ai_result_ptr
            );
            uint64_t upload_end_time = rtc_get_uptime_ms();
            uint64_t upload_duration = upload_end_time - upload_start_time;

            if (mqtt_result >= 0) {
                LOG_SVC_INFO("[TIMING] Image uploaded successfully (msg_id: %d, upload duration: %lu ms)", 
                            mqtt_result, (unsigned long)upload_duration);
                upload_result = AICAM_OK;
            } else {
                LOG_SVC_ERROR("[TIMING] Image upload failed: %d (upload duration: %lu ms)", 
                             mqtt_result, (unsigned long)upload_duration);
                upload_result = AICAM_ERROR;
            }
        } else {
            // Large image - chunked upload
            uint32_t actual_chunk_size = (chunk_size > 0) ? chunk_size : (10 * 1024); // Default 10KB
            LOG_SVC_INFO("[TIMING] Using chunked upload (size: %u bytes, chunk: %u bytes)", 
                        jpeg_size, actual_chunk_size);
            
            mqtt_result = mqtt_service_publish_image_chunked(
                NULL,
                jpeg_buffer,
                jpeg_size,
                &metadata,
                ai_result_ptr,
                actual_chunk_size
            );
            uint64_t upload_end_time = rtc_get_uptime_ms();
            uint64_t upload_duration = upload_end_time - upload_start_time;

            if (mqtt_result > 0) {
                LOG_SVC_INFO("[TIMING] Image uploaded in %d chunks (upload duration: %lu ms)", 
                            mqtt_result, (unsigned long)upload_duration);
                upload_result = AICAM_OK;
            } else {
                LOG_SVC_ERROR("[TIMING] Chunked upload failed: %d (upload duration: %lu ms)", 
                             mqtt_result, (unsigned long)upload_duration);
                upload_result = AICAM_ERROR;
            }
        }
        step_end_time = rtc_get_uptime_ms();
        step_duration = step_end_time - step_start_time;
        LOG_SVC_INFO("[TIMING] Step 4 COMPLETED: MQTT upload finished (duration: %lu ms)", 
                     (unsigned long)step_duration);
    } 
                
        

    // Step 5: Cleanup
    step_start_time = rtc_get_uptime_ms();
    LOG_SVC_INFO("[TIMING] Step 5: Cleaning up...");
    device_service_camera_free_jpeg_buffer(jpeg_buffer);
    step_end_time = rtc_get_uptime_ms();
    step_duration = step_end_time - step_start_time;
    LOG_SVC_INFO("[TIMING] Step 5 COMPLETED: Cleanup finished (duration: %lu ms)", 
                 (unsigned long)step_duration);

    // Step 6: Wait for publish confirmation (if upload was successful)
    if (upload_result == AICAM_OK) {
        step_start_time = rtc_get_uptime_ms();
        LOG_SVC_INFO("[TIMING] Step 6: Waiting for publish confirmation...");
        if(mqtt_service_wait_for_event(MQTT_EVENT_PUBLISHED, AICAM_TRUE, 10000) != AICAM_OK){
            LOG_SVC_ERROR("[TIMING] Step 6 FAILED: Wait for published event failed");
            upload_result = AICAM_ERROR;
        } else {
            step_end_time = rtc_get_uptime_ms();
            step_duration = step_end_time - step_start_time;
            LOG_SVC_INFO("[TIMING] Step 6 COMPLETED: Publish confirmation received (duration: %lu ms)", 
                         (unsigned long)step_duration);
        }
    }

    // Calculate and print total duration
    uint64_t total_end_time = rtc_get_uptime_ms();
    uint64_t total_duration = total_end_time - total_start_time;

    printf("task end time %lu ms\r\n", (unsigned long)total_end_time);
    
    if (upload_result == AICAM_OK) {
        LOG_SVC_INFO("========== Image capture and upload completed successfully ==========");
        LOG_SVC_INFO("[TIMING] TOTAL DURATION: %lu ms (%.2f seconds)", 
                     (unsigned long)total_duration, total_duration / 1000.0f);
    } else {
        LOG_SVC_ERROR("========== Image capture and upload failed: %d ==========", upload_result);
        LOG_SVC_ERROR("[TIMING] TOTAL DURATION: %lu ms (%.2f seconds)", 
                     (unsigned long)total_duration, total_duration / 1000.0f);
    }

    return upload_result;
}

