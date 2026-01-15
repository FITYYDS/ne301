#ifndef __PWR_MANAGER_H
#define __PWR_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sys_config.h"

#define PWR_WAKEUP_FLAG_STANDBY         (1 << 0)
#define PWR_WAKEUP_FLAG_STOP2           (1 << 1)
#define PWR_WAKEUP_FLAG_RTC_TIMING      (1 << 2)
#define PWR_WAKEUP_FLAG_RTC_ALARM_A     (1 << 3)
#define PWR_WAKEUP_FLAG_RTC_ALARM_B     (1 << 4)
#define PWR_WAKEUP_FLAG_CONFIG_KEY      (1 << 5)
#define PWR_WAKEUP_FLAG_PIR_HIGH        (1 << 6)
#define PWR_WAKEUP_FLAG_PIR_LOW         (1 << 7)
#define PWR_WAKEUP_FLAG_PIR_RISING      (1 << 8)
#define PWR_WAKEUP_FLAG_PIR_FALLING     (1 << 9)
#define PWR_WAKEUP_FLAG_SI91X           (1 << 10)
#define PWR_WAKEUP_FLAG_NET             (1 << 11)
#define PWR_WAKEUP_FLAG_KEY_LONG_PRESS  (1 << 12)
#define PWR_WAKEUP_FLAG_KEY_MAX_PRESS   (1 << 13)
#define PWR_WAKEUP_FLAG_WUFI            (1 << 27)
#define PWR_WAKEUP_FLAG_IWDG            (1 << 30)
#define PWR_WAKEUP_FLAG_VALID           (1 << 31)

#define PWR_3V3_SWITCH_BIT              (1 << 0)
#define PWR_WIFI_SWITCH_BIT             (1 << 1)
#define PWR_AON_SWITCH_BIT              (1 << 2)
#define PWR_N6_SWITCH_BIT               (1 << 3)
#define PWR_EXT_SWITCH_BIT              (1 << 4)
#define PWR_ALL_SWITCH_BIT              (PWR_3V3_SWITCH_BIT | PWR_WIFI_SWITCH_BIT | PWR_AON_SWITCH_BIT | PWR_N6_SWITCH_BIT | PWR_EXT_SWITCH_BIT)
#define PWR_DEFAULT_SWITCH_BITS         (PWR_3V3_SWITCH_BIT | PWR_AON_SWITCH_BIT | PWR_N6_SWITCH_BIT)

#define PWR_RTC_WAKEUP_MAX_TIME_S       (0xFFFFU)
#define PWR_RTC_WAKEUP_ADV_OFFSET_S     (1)
#define PWR_WAKEUP_KEY_LONG_PRESS_MS    (2000)
#define PWR_WAKEUP_KEY_MAX_PRESS_MS     (10000)

#define PWR_ALL_NAME                    "all"
#define PWR_WIFI_NAME                   "wifi"
#define PWR_3V3_NAME                    "3v3"
#define PWR_AON_NAME                    "aon"
#define PWR_N6_NAME                     "n6"
#define PWR_EXT_NAME                    "ext"

#define PWR_ON_NAME                     "on"
#define PWR_OFF_NAME                    "off"

#define PWR_STATE_STR(state)            ((state) == GPIO_PIN_SET ? PWR_ON_NAME : PWR_OFF_NAME)

typedef struct {
    uint8_t is_valid;       // Whether valid
    uint8_t week_day;       // Day of week (1~7), 0 means disabled (high priority)
    uint8_t date;           // Date (1~31), 0 means disabled (low priority)
    uint8_t hour;           // Hour (0~23)
    uint8_t minute;         // Minute (0~59)
    uint8_t second;         // Second (0~59)
} pwr_rtc_alarm_t;

typedef struct {
    uint32_t wakeup_time_s;
    pwr_rtc_alarm_t alarm_a;
    pwr_rtc_alarm_t alarm_b;
} pwr_rtc_wakeup_config_t;

void pwr_ctrl(const char *module, const char *state);
const char *pwr_get_state(const char *module);

uint32_t pwr_get_switch_bit(const char *module);
uint32_t pwr_get_wakeup_flags(void);
void pwr_clear_wakeup_flags(void);

void pwr_ctrl_bits(uint32_t switch_bits);
uint32_t pwr_get_switch_bits(void);

void pwr_enter_standby(uint32_t wakeup_flags, pwr_rtc_wakeup_config_t *rtc_wakeup_config);
void pwr_enter_stop2(uint32_t wakeup_flags, uint32_t switch_bits, pwr_rtc_wakeup_config_t *rtc_wakeup_config);

void pwr_n6_restart(uint32_t low_ms, uint32_t high_ms);
uint32_t pwr_usb_is_active(void);

void pwr_wait_for_key_release(void);

#ifdef __cplusplus
}
#endif
#endif /* __PWR_MANAGER_H */
