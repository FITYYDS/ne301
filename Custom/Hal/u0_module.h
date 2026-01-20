#ifndef __U0_MODULE_H__
#define __U0_MODULE_H__

#ifdef __cplusplus
    extern "C" {
#endif

#include "ms_bridging.h"

#define PWR_WAKEUP_FLAG_STANDBY         (1 << 0)        // standby mode wakeup
#define PWR_WAKEUP_FLAG_STOP2           (1 << 1)        // stop2 mode wakeup
#define PWR_WAKEUP_FLAG_RTC_TIMING      (1 << 2)        // rtc timing wakeup
#define PWR_WAKEUP_FLAG_RTC_ALARM_A     (1 << 3)        // rtc alarm A wakeup
#define PWR_WAKEUP_FLAG_RTC_ALARM_B     (1 << 4)        // rtc alarm B wakeup
#define PWR_WAKEUP_FLAG_CONFIG_KEY      (1 << 5)        // config key wakeup
#define PWR_WAKEUP_FLAG_PIR_HIGH        (1 << 6)        // pir high wakeup
#define PWR_WAKEUP_FLAG_PIR_LOW         (1 << 7)        // pir low wakeup
#define PWR_WAKEUP_FLAG_PIR_RISING      (1 << 8)        // pir rising wakeup
#define PWR_WAKEUP_FLAG_PIR_FALLING     (1 << 9)        // pir falling wakeup
#define PWR_WAKEUP_FLAG_SI91X           (1 << 10)       // wifi wakeup
#define PWR_WAKEUP_FLAG_NET             (1 << 11)       // net wakeup
#define PWR_WAKEUP_FLAG_KEY_LONG_PRESS  (1 << 12)       // key long press
#define PWR_WAKEUP_FLAG_KEY_MAX_PRESS   (1 << 13)       // key long long press
#define PWR_WAKEUP_FLAG_WUFI            (1 << 27)       // wufi wakeup
#define PWR_WAKEUP_FLAG_VALID           (1 << 31)       // wakeup flag valid

#define PWR_3V3_SWITCH_BIT              (1 << 0)        // 3v3 power switch bit
#define PWR_WIFI_SWITCH_BIT             (1 << 1)        // wifi power switch bit
#define PWR_AON_SWITCH_BIT              (1 << 2)        // n6 aon power switch bit
#define PWR_N6_SWITCH_BIT               (1 << 3)        // n6 power switch bit
#define PWR_EXT_SWITCH_BIT              (1 << 4)        // external power switch bit
#define PWR_ALL_SWITCH_BIT              (PWR_3V3_SWITCH_BIT | PWR_WIFI_SWITCH_BIT | PWR_AON_SWITCH_BIT | PWR_N6_SWITCH_BIT | PWR_EXT_SWITCH_BIT) // all power switch bits
#define PWR_DEFAULT_SWITCH_BITS         (PWR_3V3_SWITCH_BIT | PWR_AON_SWITCH_BIT | PWR_N6_SWITCH_BIT) // default power switch bits

/// @brief u0 module register
void u0_module_register(void);

/// @brief update u0 chip rtc time
/// @return 0 on success, other on error
int u0_module_update_rtc_time(void);

/// @brief n6 chip sync rtc time from u0 chip
/// @return 0 on success, other on error
int u0_module_sync_rtc_time(void);

/// @brief get u0 chip power status
/// @param switch_bits power status bits
/// @return 0 on success, other on error
int u0_module_get_power_status(uint32_t *switch_bits);

/// @brief get u0 chip power status from store
/// @return power status bits
uint32_t u0_module_get_power_status_ex(void);

/// @brief get u0 chip wakeup flag
/// @param wakeup_flag wakeup flag
/// @return 0 on success, other on error
int u0_module_get_wakeup_flag(uint32_t *wakeup_flag);

/// @brief get u0 chip wakeup flag from store
/// @return wakeup flag
uint32_t u0_module_get_wakeup_flag_ex(void);

/// @brief clear u0 chip wakeup flag
/// @return 0 on success, other on error
int u0_module_clear_wakeup_flag(void);

/// @brief reset chip n6
/// @return 0 on success, other on error
int u0_module_reset_chip_n6(void);

/// @brief get u0 chip key value
/// @param key_value key value
/// @return 0 on success, other on error
int u0_module_get_key_value(uint32_t *key_value);

/// @brief get u0 chip key value from store
/// @return key value
uint32_t u0_module_get_key_value_ex(void);

/// @brief get u0 chip pir value
/// @param pir_value pir value
/// @return 0 on success, other on error
int u0_module_get_pir_value(uint32_t *pir_value);

/// @brief get u0 chip pir value from store
uint32_t u0_module_get_pir_value_ex(void);

/// @brief get u0 chip usbin value
/// @param usbin_value usbin value
/// @return 0 on success, other on error
int u0_module_get_usbin_value(uint32_t *usbin_value);

/// @brief get u0 chip version
/// @param version version info
/// @return 0 on success, other on error
int u0_module_get_version(ms_bridging_version_t *version);

/// @brief configure u0 chip pir sensor
/// @return 0 on success, other on error
int u0_module_cfg_pir(ms_bridging_pir_cfg_t *pir_cfg);

/// @brief control u0 chip power
/// @param switch_bits power switch bits
/// @return 0 on success, other on error
int u0_module_power_control(uint32_t switch_bits);

/// @brief enter sleep mode
/// @param wakeup_flag wakeup flag, 0 means off all wakeup sources
/// @param switch_bits power keep on bits, 0 means all power off
/// @param sleep_second sleep second, 0 means off timing wakeup
/// @return 0 on success, other on error
int u0_module_enter_sleep_mode(uint32_t wakeup_flag, uint32_t switch_bits, uint32_t sleep_second);

/// @brief enter sleep mode
/// @param wakeup_flag wakeup flag, 0 means off all wakeup sources
/// @param switch_bits power keep on bits, 0 means all power off
/// @param sleep_second sleep second, 0 means off timing wakeup
/// @param rtc_alarm_a rtc alarm A
/// @param rtc_alarm_b rtc alarm B
/// @return 0 on success, other on error
int u0_module_enter_sleep_mode_ex(uint32_t wakeup_flag, uint32_t switch_bits, uint32_t sleep_second, ms_bridging_alarm_t *rtc_alarm_a, ms_bridging_alarm_t *rtc_alarm_b);

/// @brief u0 chip value change callback type
typedef void (*u0_module_value_change_cb_t)(uint32_t value);

/// @brief register u0 chip pir value change callback
/// @param pir_value_change_cb pir value change callback function
/// @return 0 on success, other on error
int u0_module_pir_value_change_cb_register(u0_module_value_change_cb_t pir_value_change_cb);

/// @brief unregister u0 chip pir value change callback
/// @param pir_value_change_cb pir value change callback function
/// @return 0 on success, other on error
int u0_module_pir_value_change_cb_unregister(void);

/// @brief register u0 chip key value change callback
/// @param key_value_change_cb key value change callback function
/// @return 0 on success, other on error
int u0_module_key_value_change_cb_register(u0_module_value_change_cb_t key_value_change_cb);

/// @brief unregister u0 chip key value change callback
/// @param key_value_change_cb key value change callback function
/// @return 0 on success, other on error
int u0_module_key_value_change_cb_unregister(void);

/// @brief u0 callback process
void u0_module_callback_process(void);

#ifdef __cplusplus
}
#endif

#endif /* __U0_MODULE_H__ */