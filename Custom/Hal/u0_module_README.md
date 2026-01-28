# U0 Module Usage Guide

## Overview

`u0_module` is an interface module for communicating with the U0 chip, providing power management, RTC time synchronization, key detection, PIR sensor reading, and sleep mode control functions.

## Main Features

- **RTC Time Management**: Time synchronization between the system and U0 chip
- **Power Management**: Control various power switch states (3V3, WiFi, AON, N6, external power, etc.)
- **Wakeup Flag Management**: Get system wakeup reason
- **Input Detection**: Read key and PIR sensor states
- **Sleep Control**: Supports Standby and Stop2 sleep modes, with timing and alarm wakeup

## Interface Description

### 1. Initialization

```c
void u0_module_register(void);
```

Initialize the U0 module, including:
- Initialize UART9 communication interface
- Create ms_bridging communication task
- Register command-line tools

### 2. RTC Time Management

#### Update U0 Chip RTC Time

```c
int u0_module_update_rtc_time(void);
```

Synchronize STM32 RTC time to U0 chip.

**Return Value**:
- `0`: Success
- Other values: Failure

**Example**:
```c
int ret = u0_module_update_rtc_time();
if (ret != 0) {
    printf("Failed to update RTC time\n");
}
```

#### Sync RTC Time from U0 Chip

```c
int u0_module_sync_rtc_time(void);
```

Synchronize U0 chip RTC time to STM32.

**Return Value**:
- `0`: Success
- Other values: Failure

**Example**:
```c
int ret = u0_module_sync_rtc_time();
if (ret == 0) {
    printf("RTC synchronized successfully\n");
}
```

### 3. Power Management

#### Get Power Status

```c
int u0_module_get_power_status(uint32_t *switch_bits);
```

Get current power switch status from U0 chip.

**Parameters**:
- `switch_bits`: Output parameter, returns power switch bitmap

**Return Value**:
- `0`: Success
- Other values: Failure

**Example**:
```c
uint32_t switch_bits = 0;
int ret = u0_module_get_power_status(&switch_bits);
if (ret == 0) {
    printf("Power status: 0x%08X\n", switch_bits);
    if (switch_bits & PWR_3V3_SWITCH_BIT) {
        printf("3V3 power is ON\n");
    }
}
```

#### Get Power Status from Cache

```c
uint32_t u0_module_get_power_status_ex(void);
```

Get power status from local cache (no actual communication).

**Return Value**: Power switch bitmap

**Example**:
```c
uint32_t power_status = u0_module_get_power_status_ex();
printf("Current power status: 0x%08X\n", power_status);
```

#### Power Control

```c
int u0_module_power_control(uint32_t switch_bits);
```

Control power switch states.

**Parameters**:
- `switch_bits`: Power switch bitmap

**Return Value**:
- `0`: Success
- Other values: Failure

**Example**:
```c
// Turn on WiFi power, keep other power states unchanged
uint32_t switch_bits = 0;
int ret = u0_module_get_power_status(&switch_bits);
if (ret == 0) {
    switch_bits |= PWR_WIFI_SWITCH_BIT;
    ret = u0_module_power_control(switch_bits);
}
// Turn off WiFi power, keep other power states unchanged
uint32_t switch_bits = 0;
int ret = u0_module_get_power_status(&switch_bits);
if (ret == 0) {
    switch_bits &= ~PWR_WIFI_SWITCH_BIT;
    ret = u0_module_power_control(switch_bits);
}
```

### 4. Wakeup Flag Management

#### Get Wakeup Flag

```c
int u0_module_get_wakeup_flag(uint32_t *wakeup_flag);
```

Get wakeup flag from U0 chip.

**Parameters**:
- `wakeup_flag`: Output parameter, returns wakeup flag bitmap

**Return Value**:
- `0`: Success
- Other values: Failure

**Wakeup Flag Definitions**:
```c
#define PWR_WAKEUP_FLAG_STANDBY         (1 << 0)    // Standby mode wakeup
#define PWR_WAKEUP_FLAG_STOP2           (1 << 1)    // Stop2 mode wakeup
#define PWR_WAKEUP_FLAG_RTC_TIMING      (1 << 2)    // RTC timing wakeup
#define PWR_WAKEUP_FLAG_RTC_ALARM_A     (1 << 3)    // RTC alarm A wakeup
#define PWR_WAKEUP_FLAG_RTC_ALARM_B     (1 << 4)    // RTC alarm B wakeup
#define PWR_WAKEUP_FLAG_CONFIG_KEY      (1 << 5)    // Config key wakeup
#define PWR_WAKEUP_FLAG_PIR_HIGH        (1 << 6)    // PIR high level wakeup
#define PWR_WAKEUP_FLAG_PIR_LOW         (1 << 7)    // PIR low level wakeup
#define PWR_WAKEUP_FLAG_PIR_RISING      (1 << 8)    // PIR rising edge wakeup
#define PWR_WAKEUP_FLAG_PIR_FALLING     (1 << 9)    // PIR falling edge wakeup
#define PWR_WAKEUP_FLAG_SI91X           (1 << 10)   // WiFi wakeup
#define PWR_WAKEUP_FLAG_NET             (1 << 11)   // Network (4G) wakeup
#define PWR_WAKEUP_FLAG_WUFI            (1 << 27)   // WUFI wakeup
#define PWR_WAKEUP_FLAG_VALID           (1 << 31)   // Wakeup flag valid
```

**Example**:
```c
uint32_t wakeup_flag = 0;
int ret = u0_module_get_wakeup_flag(&wakeup_flag);
if (ret == 0) {
    if (wakeup_flag & PWR_WAKEUP_FLAG_RTC_TIMING) {
        printf("Wakeup by RTC timing\n");
    }
    if (wakeup_flag & PWR_WAKEUP_FLAG_CONFIG_KEY) {
        printf("Wakeup by config key\n");
    }
}
```

#### Get Wakeup Flag from Cache

```c
uint32_t u0_module_get_wakeup_flag_ex(void);
```

Get wakeup flag from local cache.

**Return Value**: Wakeup flag bitmap (note: check if PWR_WAKEUP_FLAG_VALID bit has value)

### 5. Key Detection

#### Get Key Value

```c
int u0_module_get_key_value(uint32_t *key_value);
```

Get key value from U0 chip.

**Parameters**:
- `key_value`: Output parameter, returns key value

**Return Value**:
- `0`: Success
- Other values: Failure

**Example**:
```c
uint32_t key_value = 0;
int ret = u0_module_get_key_value(&key_value);
if (ret == 0) {
    printf("Key value: %u\n", key_value);
}
```

#### Get Key Value from Cache

```c
uint32_t u0_module_get_key_value_ex(void);
```

Get key value from local cache.

**Return Value**: Key value (1 = key pressed)

### 6. PIR Sensor

#### Get PIR Value

```c
int u0_module_get_pir_value(uint32_t *pir_value);
```

Get PIR sensor value from U0 chip.

**Parameters**:
- `pir_value`: Output parameter, returns PIR sensor value

**Return Value**:
- `0`: Success
- Other values: Failure

**Example**:
```c
uint32_t pir_value = 0;
int ret = u0_module_get_pir_value(&pir_value);
if (ret == 0) {
    printf("PIR value: %u\n", pir_value);
}
```

#### Get PIR Value from Cache

```c
uint32_t u0_module_get_pir_value_ex(void);
```

Get PIR value from local cache.

**Return Value**: PIR sensor value (1 = motion detected)

### 7. Sleep Mode Control

#### Basic Sleep Control

```c
int u0_module_enter_sleep_mode(uint32_t wakeup_flag, uint32_t switch_bits, uint32_t sleep_second);
```

Enter sleep mode.

**Parameters**:
- `wakeup_flag`: Wakeup flag bitmap, set to 0 to disable all wakeup sources
- `switch_bits`: Power bitmap to keep on during sleep, set to 0 to turn off all power
- `sleep_second`: Sleep duration (seconds), set to 0 to disable timing wakeup

**Return Value**:
- `0`: Success
- Other values: Failure

**Sleep Mode Description**:
- If `switch_bits` is 0 and `sleep_second <= 65535`: Enter **Standby** mode
- Otherwise: Enter **Stop2** mode

**Example**:
```c
// Example 1: Sleep for 60 seconds then wakeup, keep config key wakeup enabled
uint32_t wakeup_flags = PWR_WAKEUP_FLAG_RTC_TIMING | PWR_WAKEUP_FLAG_CONFIG_KEY;
int ret = u0_module_enter_sleep_mode(wakeup_flags, 0, 60);

// Example 2: Turn off all power, only config key can wakeup
ret = u0_module_enter_sleep_mode(PWR_WAKEUP_FLAG_CONFIG_KEY, 0, 0);

// Example 3: Keep 3V3 power on, wakeup after 120 seconds
uint32_t keep_power = PWR_3V3_SWITCH_BIT;
ret = u0_module_enter_sleep_mode(PWR_WAKEUP_FLAG_RTC_TIMING, keep_power, 120);
```

#### Extended Sleep Control (Supports RTC Alarm)

```c
int u0_module_enter_sleep_mode_ex(uint32_t wakeup_flag, uint32_t switch_bits, 
                                   uint32_t sleep_second, 
                                   ms_bridging_alarm_t *rtc_alarm_a, 
                                   ms_bridging_alarm_t *rtc_alarm_b);
```

Enter sleep mode (supports RTC alarm A and alarm B).

**Parameters**:
- `wakeup_flag`: Wakeup flag bitmap
- `switch_bits`: Power bitmap to keep on during sleep
- `sleep_second`: Sleep duration (seconds), 0 means disable timing wakeup
- `rtc_alarm_a`: RTC alarm A configuration (can be NULL)
- `rtc_alarm_b`: RTC alarm B configuration (can be NULL)

**RTC Alarm Structure**:
```c
typedef struct {
    uint8_t is_valid;       // Whether valid
    uint8_t week_day;       // Day of week (1~7), 0 means not enabled (high priority)
    uint8_t date;           // Date (1~31), 0 and week_day also 0 means daily (low priority)
    uint8_t hour;           // Hour (0~23)
    uint8_t minute;         // Minute (0~59)
    uint8_t second;         // Second (0~59)
} ms_bridging_alarm_t;
```

**Return Value**:
- `0`: Success
- Other values: Failure

**Example**:
```c
// Set daily wakeup at 8:30:00, keep config key wakeup enabled
ms_bridging_alarm_t alarm_a;
alarm_a.is_valid = 1;
alarm_a.week_day = 0;   // No weekday restriction
alarm_a.date = 0;       // No date restriction
alarm_a.hour = 8;
alarm_a.minute = 30;
alarm_a.second = 0;

uint32_t wakeup_flags = PWR_WAKEUP_FLAG_RTC_ALARM_A | PWR_WAKEUP_FLAG_CONFIG_KEY;
int ret = u0_module_enter_sleep_mode_ex(wakeup_flags, 0, 0, &alarm_a, NULL);

// Set Monday 6:00:00 wakeup
alarm_a.week_day = 1;   // Monday
alarm_a.hour = 6;
alarm_a.minute = 0;
ret = u0_module_enter_sleep_mode_ex(PWR_WAKEUP_FLAG_RTC_ALARM_A, 0, 0, &alarm_a, NULL);
```

## Power Switch Bit Definitions

```c
#define PWR_3V3_SWITCH_BIT    (1 << 0)    // 3V3 power switch
#define PWR_WIFI_SWITCH_BIT   (1 << 1)    // WiFi power switch
#define PWR_AON_SWITCH_BIT    (1 << 2)    // N6 AON power switch
#define PWR_N6_SWITCH_BIT     (1 << 3)    // N6 power switch
#define PWR_EXT_SWITCH_BIT    (1 << 4)    // External power switch

#define PWR_ALL_SWITCH_BIT    (PWR_3V3_SWITCH_BIT | PWR_WIFI_SWITCH_BIT | \
                                PWR_AON_SWITCH_BIT | PWR_N6_SWITCH_BIT | PWR_EXT_SWITCH_BIT)

#define PWR_DEFAULT_SWITCH_BITS (PWR_3V3_SWITCH_BIT | PWR_AON_SWITCH_BIT | PWR_N6_SWITCH_BIT)
```

## Complete Usage Examples

### Example 1: Sync Time on Application Startup

```c
void app_init(void)
{
    // Initialize U0 module
    u0_module_register();
    
    // Sync RTC time from U0
    int ret = u0_module_sync_rtc_time();
    if (ret != 0) {
        printf("Failed to sync RTC time: %d\n", ret);
    } else {
        printf("RTC time synchronized\n");
    }
    
    // Get wakeup reason
    uint32_t wakeup_flag = u0_module_get_wakeup_flag_ex();
    if (wakeup_flag & PWR_WAKEUP_FLAG_VALID) {
        printf("Wakeup reason: 0x%08X\n", wakeup_flag);
        
        if (wakeup_flag & PWR_WAKEUP_FLAG_RTC_TIMING) {
            printf("  - Woken by RTC timing\n");
        }
        if (wakeup_flag & PWR_WAKEUP_FLAG_CONFIG_KEY) {
            printf("  - Woken by config key\n");
        }
    }
}
```

### Example 2: Sleep for Specified Duration

```c
void enter_sleep_mode(void)
{
    // Configure wakeup sources: timing wakeup + config key wakeup
    uint32_t wakeup_flags = PWR_WAKEUP_FLAG_RTC_TIMING | PWR_WAKEUP_FLAG_CONFIG_KEY;
    
    // Turn off all power (Standby mode)
    uint32_t switch_bits = 0;
    
    // Sleep for 300 seconds (5 minutes)
    uint32_t sleep_seconds = 300;
    
    int ret = u0_module_enter_sleep_mode(wakeup_flags, switch_bits, sleep_seconds);
    if (ret != 0) {
        printf("Failed to enter sleep mode: %d\n", ret);
    } else {
        printf("Entering sleep mode...\n");
        // System will enter sleep and automatically wakeup under U0 chip control
    }
}
```

### Example 3: Wakeup at Specified Time

```c
void setup_daily_alarm(void)
{
    ms_bridging_alarm_t alarm_a;
    
    // Set wakeup at 12:00:00
    alarm_a.is_valid = 1;
    alarm_a.week_day = 0;   // No weekday restriction
    alarm_a.date = 0;       // No date restriction
    alarm_a.hour = 12;
    alarm_a.minute = 0;
    alarm_a.second = 0;
    
    uint32_t wakeup_flags = PWR_WAKEUP_FLAG_RTC_ALARM_A | PWR_WAKEUP_FLAG_CONFIG_KEY;
    
    int ret = u0_module_enter_sleep_mode_ex(wakeup_flags, 0, 0, &alarm_a, NULL);
    if (ret != 0) {
        printf("Failed to setup daily alarm: %d\n", ret);
    }
}
```

### Example 4: Power Management

```c
void power_management_example(void)
{
    uint32_t power_status;
    
    // Get current power status
    int ret = u0_module_get_power_status(&power_status);
    if (ret == 0) {
        printf("Current power status: 0x%08X\n", power_status);
        
        // Check each power status
        if (power_status & PWR_3V3_SWITCH_BIT) {
            printf("3V3 power is ON\n");
        }
        if (power_status & PWR_WIFI_SWITCH_BIT) {
            printf("WiFi power is ON\n");
        }
    }
    
    // Turn on WiFi power
    power_status |= PWR_WIFI_SWITCH_BIT;
    ret = u0_module_power_control(power_status);
    if (ret != 0) {
        printf("Failed to turn on WiFi\n");
    }
    
    // Turn off WiFi power
    power_status &= ~PWR_WIFI_SWITCH_BIT;
    ret = u0_module_power_control(power_status);
    if (ret != 0) {
        printf("Failed to turn off WiFi\n");
    }
}
```

### Example 5: PIR Wakeup

#### 5.1 Configure PIR Sensor and Enter Sleep (PIR Rising Edge Wakeup)

```c
void pir_wakeup_example(void)
{
    int ret = 0;
    ms_bridging_pir_cfg_t pir_cfg = {0};
    
    // Configure PIR sensor parameters
    pir_cfg.sensitivity_level = 30;    // Sensitivity level (recommended >30, smaller value means more sensitive)
    pir_cfg.ignore_time_s = 7;         // Ignore time after interrupt (0.5 + 0.5 * 7 seconds)
    pir_cfg.pulse_count = 1;           // Pulse count (1-4)
    pir_cfg.window_time_s = 0;          // Window time (2 + 0 seconds)
    pir_cfg.motion_enable = 1;          // Enable motion detection
    pir_cfg.interrupt_src = 0;          // Interrupt source: 0=motion detection, 1=raw data
    pir_cfg.volt_select = 0;           // ADC selection: 0=PIR signal BFP output
    
    ret = u0_module_cfg_pir(&pir_cfg);
    if (ret != 0) {
        printf("Failed to configure PIR: %d\n", ret);
        return;
    }
    printf("PIR configured successfully\n");
    
    // Configure wakeup sources: PIR rising edge wakeup + config key wakeup (as backup)
    uint32_t wakeup_flags = PWR_WAKEUP_FLAG_PIR_RISING | PWR_WAKEUP_FLAG_CONFIG_KEY;
    
    // Keep 3V3 power on (PIR sensor needs power)
    // uint32_t switch_bits = PWR_3V3_SWITCH_BIT;
    
    // Optional: Set timing wakeup as backup (wakeup after 300 seconds)
    uint32_t sleep_seconds = 300;
    
    ret = u0_module_enter_sleep_mode(wakeup_flags, switch_bits, sleep_seconds);
    if (ret != 0) {
        printf("Failed to enter PIR wakeup sleep mode: %d\n", ret);
    } else {
        printf("Entering sleep mode, waiting for PIR trigger...\n");
        // System will enter sleep and automatically wakeup when PIR detects motion (rising edge)
    }
}
```

#### 5.2 Check PIR Wakeup Reason on Application Startup

```c
void check_pir_wakeup_reason(void)
{
    // Get wakeup flag
    uint32_t wakeup_flag = u0_module_get_wakeup_flag_ex();
    
    if (wakeup_flag & PWR_WAKEUP_FLAG_VALID) {
        printf("Wakeup reason: 0x%08X\n", wakeup_flag);
        
        // Check if woken by PIR
        if (wakeup_flag & PWR_WAKEUP_FLAG_PIR_RISING) {
            printf("  - Woken by PIR rising edge (motion detected)\n");
            
            // Read current PIR value
            uint32_t pir_value = u0_module_get_pir_value_ex();
            printf("  - Current PIR value: %u\n", pir_value);
        }
        if (wakeup_flag & PWR_WAKEUP_FLAG_PIR_FALLING) {
            printf("  - Woken by PIR falling edge (motion ended)\n");
        }
        if (wakeup_flag & PWR_WAKEUP_FLAG_PIR_HIGH) {
            printf("  - Woken by PIR high level\n");
        }
        if (wakeup_flag & PWR_WAKEUP_FLAG_PIR_LOW) {
            printf("  - Woken by PIR low level\n");
        }
    }
}
```

#### 5.3 Use Different PIR Wakeup Modes

```c
void pir_wakeup_modes_example(void)
{
    int ret = 0;
    uint32_t wakeup_flags = 0;
    uint32_t switch_bits = 0;// PWR_3V3_SWITCH_BIT;
    
    // Method 1: PIR rising edge wakeup
    wakeup_flags = PWR_WAKEUP_FLAG_PIR_RISING | PWR_WAKEUP_FLAG_CONFIG_KEY;
    ret = u0_module_enter_sleep_mode(wakeup_flags, switch_bits, 0);
    
    // Method 2: PIR falling edge wakeup
    // wakeup_flags = PWR_WAKEUP_FLAG_PIR_FALLING | PWR_WAKEUP_FLAG_CONFIG_KEY;
    // ret = u0_module_enter_sleep_mode(wakeup_flags, switch_bits, 0);
    
    // Method 3: PIR high level wakeup
    // wakeup_flags = PWR_WAKEUP_FLAG_PIR_HIGH | PWR_WAKEUP_FLAG_CONFIG_KEY;
    // ret = u0_module_enter_sleep_mode(wakeup_flags, switch_bits, 0);
    
    // Method 4: PIR low level wakeup
    // wakeup_flags = PWR_WAKEUP_FLAG_PIR_LOW | PWR_WAKEUP_FLAG_CONFIG_KEY;
    // ret = u0_module_enter_sleep_mode(wakeup_flags, switch_bits, 0);
    
    // Method 5: PIR rising edge + timing wakeup (dual wakeup sources)
    // wakeup_flags = PWR_WAKEUP_FLAG_PIR_RISING | PWR_WAKEUP_FLAG_RTC_TIMING | PWR_WAKEUP_FLAG_CONFIG_KEY;
    // ret = u0_module_enter_sleep_mode(wakeup_flags, switch_bits, 600); // Timing wakeup after 600 seconds
    
    if (ret != 0) {
        printf("Failed to enter sleep mode: %d\n", ret);
    }
}
```

## Command-Line Tool Usage

The U0 module provides command-line tools for debugging and testing. The following commands can be used through UART or other CLI interfaces in the system:

### Command List

| Command | Description |
|------|------|
| `u0` | Show help information |
| `u0 key` | Get key value |
| `u0 pir` | Get PIR sensor value |
| `u0 pwr` | Get power status |
| `u0 pwr_on <name1> ... <nameN>` | Turn on specified power |
| `u0 pwr_off <name1> ... <nameN>` | Turn off specified power |
| `u0 wakeup_flag` | Get wakeup flag |
| `u0 rtc_update` | Update STM32 RTC time to U0 chip |
| `u0 rtc_sync` | Sync RTC time from U0 chip to STM32 |
| `u0 sleep <seconds> [name1] ...` | Enter sleep mode (timing wakeup) |
| `u0 sleep_ex <date> <week_day> <hour> <minute> <second> [name1] ...` | Enter sleep mode (alarm wakeup) |
| `u0 cfg_pir [sensitivity_level] [ignore_time_s] [pulse_count] [window_time_s]` | Configure PIR sensor parameters |
| `u0 sleep_pir [sleep_second]` | Enter sleep mode (PIR rising edge wakeup) |

### Command Examples

#### Get Input Status
```
# Get key value
> u0 key
key value: 0

# Get PIR value
> u0 pir
pir value: 1
```

#### Power Control
```
# Get current power status
> u0 pwr
power status: 0000000B

# Turn on WiFi power
> u0 pwr_on wifi
before power on, status: 0000000B
after power on, status: 0000000D

# Turn off WiFi power
> u0 pwr_off wifi
before power off, status: 0000000D
after power off, status: 0000000B
```

#### RTC Time Management
```
# Update STM32 RTC time to U0 chip
> u0 rtc_update
update rtc time success

# Sync RTC time from U0 chip
> u0 rtc_sync
sync rtc time success
```

#### Enter Sleep Mode (Timing Wakeup)
```
# Sleep for 60 seconds then wakeup
> u0 sleep 60

# Keep 3V3 and WiFi power on, sleep for 120 seconds then wakeup (WiFi wakeup will be automatically configured when 3V3 and WiFi power are kept on)
> u0 sleep 120 3v3 wifi

# Support WiFi wakeup, keep 3V3 power on
> u0 sleep 300 3v3
```

#### Enter Sleep Mode (Alarm Wakeup)
```
# Set wakeup at 8:30:00
> u0 sleep_ex 0 0 8 30 0

# Set wakeup on Monday at 6:00:00
> u0 sleep_ex 0 1 6 0 0

# Keep 3V3 power on, wakeup at 12:00:00 (WiFi wakeup will be automatically configured when 3V3 and WiFi power are kept on)
> u0 sleep_ex 0 0 12 0 0 3v3
```

#### PIR Sensor Configuration and Wakeup

```
# Configure PIR sensor (using default parameters)
> u0 cfg_pir
configure pir success

# Configure PIR sensor (custom parameters)
# Parameters: sensitivity_level(30) ignore_time(0.5 + 0.5 * 7 seconds) pulse_count(1) window_time(2 + 0 seconds)
> u0 cfg_pir 30 7 1 0
configure pir success

# Enter sleep mode, use PIR rising edge wakeup (need to configure PIR first)
> u0 sleep_pir
enter sleep pir mode success

# Enter sleep mode, PIR wakeup + 300 second timing wakeup (dual wakeup sources)
> u0 sleep_pir 300
enter sleep pir mode success

# Check wakeup reason (if woken by PIR)
> u0 wakeup_flag
wakeup flag: 80000100
```

**PIR Wakeup Flag Description**:
- `PWR_WAKEUP_FLAG_PIR_RISING` (0x100): PIR rising edge wakeup
- `PWR_WAKEUP_FLAG_PIR_FALLING` (0x200): PIR falling edge wakeup
- `PWR_WAKEUP_FLAG_PIR_HIGH` (0x40): PIR high level wakeup
- `PWR_WAKEUP_FLAG_PIR_LOW` (0x80): PIR low level wakeup

### Power Name List

| Name | Description | Bit Definition |
|------|------|--------|
| `3v3` | 3V3 power | `PWR_3V3_SWITCH_BIT` |
| `wifi` | WiFi power | `PWR_WIFI_SWITCH_BIT` |
| `aon` | N6 AON power | `PWR_AON_SWITCH_BIT` |
| `n6` | N6 power | `PWR_N6_SWITCH_BIT` |
| `ext` | External power | `PWR_EXT_SWITCH_BIT` |
| `all` | All power | `PWR_ALL_SWITCH_BIT` |

## Notes

1. **Initialization Order**: Before using any U0 module functions, `u0_module_register()` must be called first for initialization. (Application does not need to care)

2. **Interrupt Handling**: The U0 module uses UART9 for communication, `u0_module_IRQHandler()` needs to be called in UART9 interrupt:
   ```c
   void UART9_IRQHandler(void)
   {
       HAL_UART_IRQHandler(&huart9);
       u0_module_IRQHandler(&huart9);
   }
   ```

3. **Sleep Mode**:
   - Standby mode: Lowest power consumption, but system completely resets after wakeup (application does not need to care)
   - Stop2 mode: Can maintain some peripheral states, can restore context after wakeup (application does not need to care)
   - During sleep, only configured wakeup sources can wake up the system

4. **Power Switches**:
   - Turning off a power may cause peripherals related to that power to stop working
   - Turning off all power will enter low power mode
   - WiFi and 3V3 power need to be kept on when network functions are working

5. **Time Synchronization**:
   - It is recommended to sync RTC time on system startup
   - If the device has an accurate clock source, time should be synced periodically

6. **Wakeup Flags**:
   - Wakeup flags are automatically updated after system wakeup
   - Wakeup reason can be understood by querying wakeup flags
   - Wakeup flags need to be reconfigured before next sleep

## Error Handling

All API return value conventions:
- `0`: Operation successful
- Non-zero value: Operation failed, return value is error code

It is recommended to check for errors after use:

```c
int ret = u0_module_sync_rtc_time();
if (ret != 0) {
    printf("Error code: %d\n", ret);
    // Handle error
}
```

## Dependencies

- UART9 configuration (`usart.h`)
- RTC configuration (`rtc.h`)
- FreeRTOS (`tx_port.h`)
- ms_bridging communication protocol (`ms_bridging.h`)
- Memory management (`Hal/mem.h`)
- Logging system (`Log/debug.h`)

## Version History

- V1.0: Initial version, supports basic functions
