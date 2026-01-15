# 4G Network Card Usage Guide

## Overview

This document describes how to use the 4G network card module (based on EG912U module), including initialization, information/configuration retrieval, connection, disconnection, and AT command sending functions.

## Important Notes

⚠️ **State Requirements**:
- **Get Information/Configuration**: Information/configuration can be retrieved in any state, but note:
  - **Network Information** (IP address, gateway, subnet mask): Only correct when in **UP** state (automatically obtained through PPP negotiation, 4G network card does not support configuring these parameters)
  - **Device Information** (IMEI, IMSI, ICCID, signal strength, SIM card status, etc.): Latest data can only be obtained when in **DOWN** state
  - When in **UP** state, device information is cached data from initialization or the last **DOWN** state
- **Set Configuration**: When using the `nm_set_netif_cfg()` function, **no manual state checking is required**. The function internally handles the down->cfg->up flow automatically
- **Send AT Commands**: Must be used only when the network card is in **disconnected (DOWN)** state, and the modem must be in **INIT** state

## 1. Initialization

### 1.1 API Method

```c
#include "netif_manager.h"

int ret = nm_ctrl_netif_init("4g");
if (ret != 0) {
    // Initialization failed
    printf("4G network card initialization failed: %d\n", ret);
}
```

### 1.2 Command Line Method

```bash
ifconfig 4g init
```

**Notes**:
- Initialization detects and sets UART baud rate (supports 115200, 230400, 460800, 921600)
- Initialization retrieves basic network card information (model, IMEI, firmware version, etc.)
- After successful initialization, the network card state is **DOWN**

## 2. Get Information/Configuration

### 2.1 Get Network Card Information

**Notes**: Network card information can be retrieved in any state, but note the accuracy of different information:
- **Network Information** (IP address, gateway, subnet mask): Only correct when in **UP** state (automatically obtained through PPP negotiation, 4G network card does not support configuring these parameters)
- **Device Information** (IMEI, IMSI, ICCID, signal strength, SIM card status, etc.): Latest data can only be obtained when in **DOWN** state
- When in **UP** state, device information is cached data from initialization or the last **DOWN** state

#### API Method

```c
#include "netif_manager.h"

// Get network card information (can be retrieved in any state)
netif_info_t netif_info = {0};
int ret = nm_get_netif_info("4g", &netif_info);
if (ret == 0) {
    // Print information
    nm_print_netif_info("4g", NULL);
    
    // Network information (only correct when in UP state, automatically obtained through PPP negotiation)
    printf("IP Address: %d.%d.%d.%d\n",
           netif_info.ip_addr[0], netif_info.ip_addr[1],
           netif_info.ip_addr[2], netif_info.ip_addr[3]);
    printf("Gateway: %d.%d.%d.%d\n",
           netif_info.gw[0], netif_info.gw[1],
           netif_info.gw[2], netif_info.gw[3]);
    printf("Subnet Mask: %d.%d.%d.%d\n",
           netif_info.netmask[0], netif_info.netmask[1],
           netif_info.netmask[2], netif_info.netmask[3]);
    
    // Device information (latest data can only be obtained when in DOWN state)
    printf("Model: %s\n", netif_info.cellular_info.model_name);
    printf("IMEI: %s\n", netif_info.cellular_info.imei);
    printf("IMSI: %s\n", netif_info.cellular_info.imsi);
    printf("ICCID: %s\n", netif_info.cellular_info.iccid);
    printf("SIM Status: %s\n", netif_info.cellular_info.sim_status);
    printf("Operator: %s\n", netif_info.cellular_info.operator);
    printf("Signal Strength: %d dBm\n", netif_info.cellular_info.rssi);
    printf("CSQ Value: %d, BER: %d\n", netif_info.cellular_info.csq_value, 
           netif_info.cellular_info.ber_value);
    printf("Signal Level: %d\n", netif_info.cellular_info.csq_level);
    printf("Firmware Version: %s\n", netif_info.fw_version);
    
    // Configuration information is also included in netif_info
    printf("APN: %s\n", netif_info.cellular_cfg.apn);
    printf("Username: %s\n", netif_info.cellular_cfg.user);
    printf("Password: %s\n", netif_info.cellular_cfg.passwd);
    printf("Authentication: %d\n", netif_info.cellular_cfg.authentication);
    printf("Roaming: %s\n", netif_info.cellular_cfg.is_enable_roam ? "Enabled" : "Disabled");
}

// If you need to get the latest device information (signal strength, SIM card status, etc.), disconnect first
netif_state_t state = nm_get_netif_state("4g");
if (state == NETIF_STATE_UP) {
    // Disconnect first to get latest device information
    nm_ctrl_netif_down("4g");
    ret = nm_get_netif_info("4g", &netif_info);
    // Now you can get the latest signal strength, SIM card status, and other device information
}
```

#### Command Line Method

```bash
# Get and print information (can be retrieved in any state)
# In UP state: IP address and other information are correct, device information is cached data
# In DOWN state: Device information is latest, IP address and other information may be empty or incorrect
ifconfig 4g info

# If you need to get the latest device information (signal strength, SIM card status, etc.), disconnect first then get
ifconfig 4g down
ifconfig 4g info
```

**Information Field Descriptions**:

**Network Information** (only correct when in UP state, automatically obtained through PPP negotiation, 4G network card does not support configuring these parameters):
- `ip_addr`: IP address (automatically obtained through PPP negotiation)
- `gw`: Gateway address (automatically obtained through PPP negotiation)
- `netmask`: Subnet mask (automatically obtained through PPP negotiation)

**Device Information** (latest data can only be obtained when in DOWN state):
- `model_name`: Device model name
- `imei`: Device IMEI number
- `imsi`: SIM card IMSI number
- `iccid`: SIM card ICCID number
- `sim_status`: SIM card status (READY/No SIM Card, etc.)
- `operator`: Current network operator name
- `rssi`: Received Signal Strength Indicator (dBm)
- `csq_value`: Signal strength value (0~31, 99 indicates no signal)
- `ber_value`: Bit Error Rate value
- `csq_level`: Signal strength level (0~5)
- `version`: Firmware version

**Configuration Information**:
- `cellular_cfg`: Cellular network configuration (APN, username, password, etc.)

### 2.2 Get Network Card Configuration

**Notes**: Network card configuration can be retrieved in any state. Configuration information (APN, username, password, etc.) can only be obtained as latest data when in DOWN state. Usually, you can directly get configuration information from the `cellular_cfg` field of the `netif_info_t` structure without separately calling `nm_get_netif_cfg()`. Only use `nm_get_netif_cfg()` when you need to get configuration separately (without getting other information).

#### Method 1: Get Configuration from Information Retrieval (Recommended)

```c
#include "netif_manager.h"

// Get information (can be retrieved in any state)
// Note: If you need to get latest configuration in UP state, disconnect the network card first
netif_state_t state = nm_get_netif_state("4g");
if (state == NETIF_STATE_UP) {
    // If the network card is in UP state and you need latest configuration, disconnect first
    // Otherwise, you get cached data
    nm_ctrl_netif_down("4g");
}

// Get information (includes configuration information)
netif_info_t netif_info = {0};
int ret = nm_get_netif_info("4g", &netif_info);
if (ret == 0) {
    // Get configuration from netif_info
    printf("APN: %s\n", netif_info.cellular_cfg.apn);
    printf("Username: %s\n", netif_info.cellular_cfg.user);
    printf("Password: %s\n", netif_info.cellular_cfg.passwd);
    printf("Authentication: %d\n", netif_info.cellular_cfg.authentication);
    printf("Roaming: %s\n", netif_info.cellular_cfg.is_enable_roam ? "Enabled" : "Disabled");
    printf("PIN Code: %s\n", netif_info.cellular_cfg.pin);
    printf("PUK Code: %s\n", netif_info.cellular_cfg.puk);
}
```

#### Method 2: Get Configuration Separately (Use only when needed)

```c
#include "netif_manager.h"

// Get configuration separately (can be retrieved in any state)
// Note: If you need to get latest configuration in UP state, disconnect the network card first
netif_state_t state = nm_get_netif_state("4g");
if (state == NETIF_STATE_UP) {
    // If the network card is in UP state and you need latest configuration, disconnect first
    // Otherwise, you get cached data
    nm_ctrl_netif_down("4g");
}

// Get configuration separately (use only when you need to get configuration separately)
netif_config_t netif_cfg = {0};
int ret = nm_get_netif_cfg("4g", &netif_cfg);
if (ret == 0) {
    printf("APN: %s\n", netif_cfg.cellular_cfg.apn);
    printf("Username: %s\n", netif_cfg.cellular_cfg.user);
    printf("Password: %s\n", netif_cfg.cellular_cfg.passwd);
    printf("Authentication: %d\n", netif_cfg.cellular_cfg.authentication);
    printf("Roaming: %s\n", netif_cfg.cellular_cfg.is_enable_roam ? "Enabled" : "Disabled");
    printf("PIN Code: %s\n", netif_cfg.cellular_cfg.pin);
    printf("PUK Code: %s\n", netif_cfg.cellular_cfg.puk);
}
```

#### Command Line Method

```bash
# Get configuration (can be retrieved in any state, configuration information can be seen through info command)
# Note: In UP state, cached data is retrieved
ifconfig 4g info

# If you need to get latest configuration, disconnect first then get
ifconfig 4g down
ifconfig 4g info
```

**Configuration Field Descriptions**:
- `apn`: APN (Access Point Name)
- `user`: APN username
- `passwd`: APN password
- `authentication`: APN authentication method (0=None, 1=PAP, 2=CHAP, etc.)
- `is_enable_roam`: Whether roaming is enabled (0=Disabled, 1=Enabled)
- `pin`: SIM card PIN code
- `puk`: SIM card PUK code

## 3. Set Configuration

### 3.1 API Method

**Notes**: The `nm_set_netif_cfg()` function internally handles state transitions automatically. If the network card is currently in UP state, the function will automatically down first, then set configuration, and finally automatically up if it was previously in UP state. **No manual state checking or calling down/up is required**.

```c
#include "netif_manager.h"

// Method 1: Get configuration from information retrieval (recommended)
// Note: Information retrieval must be in DOWN state
netif_state_t state = nm_get_netif_state("4g");
if (state != NETIF_STATE_DOWN) {
    nm_ctrl_netif_down("4g");
}

netif_info_t netif_info = {0};
nm_get_netif_info("4g", &netif_info);

// Copy configuration to netif_config_t structure
netif_config_t netif_cfg = {0};
memcpy(&netif_cfg.cellular_cfg, &netif_info.cellular_cfg, sizeof(netif_cfg.cellular_cfg));

// Modify configuration
strncpy(netif_cfg.cellular_cfg.apn, "cmnet", sizeof(netif_cfg.cellular_cfg.apn) - 1);
strncpy(netif_cfg.cellular_cfg.user, "user", sizeof(netif_cfg.cellular_cfg.user) - 1);
strncpy(netif_cfg.cellular_cfg.passwd, "pass", sizeof(netif_cfg.cellular_cfg.passwd) - 1);
netif_cfg.cellular_cfg.authentication = 1;  // PAP authentication
netif_cfg.cellular_cfg.is_enable_roam = 0; // Disable roaming

// Set configuration (function internally handles down->cfg->up)
int ret = nm_set_netif_cfg("4g", &netif_cfg);
if (ret != 0) {
    printf("Set configuration failed: %d\n", ret);
}
```

**Or use the method of getting configuration separately**:

```c
// Method 2: Get configuration separately (use only when needed)
// Note: Configuration retrieval must be in DOWN state
netif_state_t state = nm_get_netif_state("4g");
if (state != NETIF_STATE_DOWN) {
    nm_ctrl_netif_down("4g");
}

netif_config_t netif_cfg = {0};
nm_get_netif_cfg("4g", &netif_cfg);

// Modify configuration
strncpy(netif_cfg.cellular_cfg.apn, "cmnet", sizeof(netif_cfg.cellular_cfg.apn) - 1);
// ... other configuration modifications

// Set configuration (function internally handles down->cfg->up)
nm_set_netif_cfg("4g", &netif_cfg);
```

### 3.2 Command Line Method

```bash
# Ensure network card is in DOWN state (required for getting configuration)
ifconfig 4g down

# Set APN (only supports setting APN, equivalent to calling nm_set_netif_cfg)
ifconfig 4g cfg cmnet
```

**Notes**: The command line `cfg` command only supports setting APN. Other configurations need to use the API method. The `cfg` command internally handles the down->cfg->up flow automatically.

## 4. Connect (Start Network Card)

### 4.1 API Method

```c
#include "netif_manager.h"

// Ensure network card is initialized and in DOWN state
netif_state_t state = nm_get_netif_state("4g");
if (state == NETIF_STATE_DEINIT) {
    // Initialize first
    nm_ctrl_netif_init("4g");
}

// Connect network card (function internally waits for network card ready, including SIM card ready, PPP connection establishment, etc.)
int ret = nm_ctrl_netif_up("4g");
if (ret == 0) {
    printf("4G network card connected successfully\n");
    
    // Get IP information after connection
    netif_info_t netif_info = {0};
    nm_get_netif_info("4g", &netif_info);
    printf("IP Address: %d.%d.%d.%d\n", 
           netif_info.ip_addr[0], netif_info.ip_addr[1],
           netif_info.ip_addr[2], netif_info.ip_addr[3]);
    printf("Gateway: %d.%d.%d.%d\n",
           netif_info.gw[0], netif_info.gw[1],
           netif_info.gw[2], netif_info.gw[3]);
} else {
    printf("4G network card connection failed: %d\n", ret);
}
```

**Connection Flow Description**:
1. Wait for SIM card ready (wait up to 10 seconds)
2. Set APN and other configuration parameters
3. Update network card information
4. Enter PPP mode
5. Establish PPP connection
6. Get IP address (through PPP negotiation)
7. **Note**: The `nm_ctrl_netif_up()` function internally waits for network card ready, including SIM card ready, PPP connection establishment, etc. No additional delay is needed

### 4.2 Command Line Method

```bash
ifconfig 4g up
```

## 5. Disconnect (Stop Network Card)

### 5.1 API Method

```c
#include "netif_manager.h"

int ret = nm_ctrl_netif_down("4g");
if (ret == 0) {
    printf("4G network card disconnected\n");
}
```

**Disconnection Flow Description**:
1. Close PPP connection
2. Exit PPP mode
3. Network card state changes to DOWN

### 5.2 Command Line Method

```bash
ifconfig 4g down
```

## 6. Send AT Commands

### 6.1 Using Modem Command Line Tool

**⚠️ Important: AT commands can only be used when the network card is in disconnected (DOWN) state, and the modem must be in INIT state**

#### Basic Usage

```bash
# Ensure network card is in DOWN state
ifconfig 4g down

# Send AT command (basic format)
modem AT<command>

# Example: Query signal strength
modem AT+CSQ

# Example: Query SIM card status
modem AT+CPIN?

# Example: Query IMEI
modem AT+GSN

# Example: Query APN configuration
modem AT+CGDCONT?
```

#### Advanced Usage (with parameters)

```bash
# Set timeout (-t parameter, unit: milliseconds)
modem AT+CSQ -t 1000

# Set expected response lines (-r parameter)
modem AT+CGDCONT? -r 2

# Combined usage
modem AT+CSQ -t 2000 -r 1
```

**Parameter Descriptions**:
- `-t <timeout>`: Set command timeout (milliseconds), default 500ms
- `-r <num>`: Set expected response lines, default 1 line

#### Common AT Command Examples

```bash
# Query signal strength
modem AT+CSQ

# Query SIM card status
modem AT+CPIN?

# Query IMEI
modem AT+GSN

# Query IMSI
modem AT+CIMI

# Query ICCID
modem AT+QCCID

# Query operator information
modem AT+COPS?

# Query APN configuration
modem AT+CGDCONT?

# Query APN authentication configuration
modem AT+QICSGP=1

# Set APN
modem AT+CGDCONT=1,"IP","cmnet"

# Set APN authentication
modem AT+QICSGP=1,1,"cmnet","user","pass",1

# Query firmware version
modem AT+CGMR

# Query device model
modem AT+CGMM

# Test AT communication
modem AT
```

### 6.2 API Method (Reference Implementation)

If you need to send AT commands through API, you can refer to the implementation of the `modem_cmd_deal_cmd` function in `ms_modem.c`:

```c
#include "ms_modem.h"
#include "ms_modem_at.h"

// Ensure modem is in INIT state
modem_state_t modem_state = modem_device_get_state();
if (modem_state != MODEM_STATE_INIT) {
    printf("modem is not in init state!\n");
    return -1;
}

// Prepare AT command and response buffers
char at_cmd[MODEM_AT_CMD_LEN_MAXIMUM] = {0};
char *rsp_list[MODEM_AT_RSP_MAX_LINE_NUM] = {0};
int rsp_num = 1;  // Expected response lines
uint32_t timeout = 500;  // Timeout (milliseconds)

// Allocate response buffers
for (int i = 0; i < rsp_num; i++) {
    rsp_list[i] = (char *)pvPortMalloc(MODEM_AT_RSP_LEN_MAXIMUM);
    if (rsp_list[i] == NULL) {
        printf("malloc rsp_list[%d] failed!\n", i);
        return -1;
    }
}

// Construct AT command (need to add \r\n)
snprintf(at_cmd, sizeof(at_cmd), "AT+CSQ\r\n");

// Send AT command and wait for response
extern modem_at_handle_t modem_at_handle;
int ret = modem_at_cmd_wait_rsp(&modem_at_handle, at_cmd, rsp_list, rsp_num, timeout);

if (ret >= MODEM_OK) {
    // Print response
    for (int i = 0; i < ret; i++) {
        printf("rsp[%d] = %s\n", i, rsp_list[i]);
    }
} else {
    printf("modem at failed(ret = %d)!\n", ret);
}

// Free response buffers
for (int i = 0; i < rsp_num; i++) {
    if (rsp_list[i] != NULL) {
        vPortFree(rsp_list[i]);
        rsp_list[i] = NULL;
    }
}
```

## 7. Complete Usage Examples

### 7.1 Initialize and Connect

```c
#include "netif_manager.h"

// 1. Initialize
int ret = nm_ctrl_netif_init("4g");
if (ret != 0) {
    printf("Initialization failed: %d\n", ret);
    return;
}

// 2. Get information (in DOWN state, includes configuration information)
netif_info_t info = {0};
ret = nm_get_netif_info("4g", &info);
if (ret == 0) {
    printf("IMEI: %s\n", info.cellular_info.imei);
    printf("Signal Strength: %d dBm\n", info.cellular_info.rssi);
    printf("Current APN: %s\n", info.cellular_cfg.apn);
}

// 3. Set configuration (if modification is needed)
// Note: If network card is already in UP state, nm_set_netif_cfg will automatically restore UP state, no manual up needed
if (strcmp(info.cellular_cfg.apn, "cmnet") != 0) {
    // Get configuration from info, modify then set
    netif_config_t cfg = {0};
    memcpy(&cfg.cellular_cfg, &info.cellular_cfg, sizeof(cfg.cellular_cfg));
    strncpy(cfg.cellular_cfg.apn, "cmnet", sizeof(cfg.cellular_cfg.apn) - 1);
    
    // Set configuration (function internally handles down->cfg->up)
    ret = nm_set_netif_cfg("4g", &cfg);
    if (ret != 0) {
        printf("Set configuration failed: %d\n", ret);
        return;
    }
    // If network card was previously in UP state, nm_set_netif_cfg will automatically restore UP, no need to up again here
    // If network card was previously in DOWN state, need to manually up
}

// 4. Connect (if network card is currently in DOWN state)
// Note: nm_ctrl_netif_up() internally waits for network card ready, including SIM card ready, PPP connection establishment, etc. No additional delay needed
ret = nm_ctrl_netif_up("4g");
if (ret == 0) {
    printf("4G network card connected successfully\n");
    
    // Get IP information after connection
    ret = nm_get_netif_info("4g", &info);
    if (ret == 0) {
        printf("IP Address: %d.%d.%d.%d\n", 
               info.ip_addr[0], info.ip_addr[1],
               info.ip_addr[2], info.ip_addr[3]);
        printf("Gateway: %d.%d.%d.%d\n",
               info.gw[0], info.gw[1],
               info.gw[2], info.gw[3]);
        printf("Subnet Mask: %d.%d.%d.%d\n",
               info.netmask[0], info.netmask[1],
               info.netmask[2], info.netmask[3]);
    }
} else {
    printf("4G network card connection failed: %d\n", ret);
}
```

### 7.2 Disconnect and Use AT Commands

```c
#include "netif_manager.h"
#include "ms_modem.h"
#include "ms_modem_at.h"

// 1. Disconnect network card
int ret = nm_ctrl_netif_down("4g");
if (ret != 0) {
    printf("Disconnect network card failed: %d\n", ret);
    return;
}

// 2. Ensure modem is in INIT state
modem_state_t state = modem_device_get_state();
if (state != MODEM_STATE_INIT) {
    printf("Modem state error: %d, current state: %d\n", state, state);
    return;
}

// 3. Send AT command (using API method)
extern modem_at_handle_t modem_at_handle;
char at_cmd[MODEM_AT_CMD_LEN_MAXIMUM] = {0};
char *rsp_list[MODEM_AT_RSP_MAX_LINE_NUM] = {0};
int rsp_num = 1;
uint32_t timeout = 500;

// Allocate response buffers
for (int i = 0; i < rsp_num; i++) {
    rsp_list[i] = (char *)pvPortMalloc(MODEM_AT_RSP_LEN_MAXIMUM);
    if (rsp_list[i] == NULL) {
        printf("Allocate response buffer failed\n");
        return;
    }
}

// Send AT+CSQ command to query signal strength
snprintf(at_cmd, sizeof(at_cmd), "AT+CSQ\r\n");
ret = modem_at_cmd_wait_rsp(&modem_at_handle, at_cmd, rsp_list, rsp_num, timeout);
if (ret >= MODEM_OK) {
    printf("Signal strength response: %s\n", rsp_list[0]);
} else {
    printf("AT command execution failed: %d\n", ret);
}

// Send AT+CPIN? command to query SIM card status
snprintf(at_cmd, sizeof(at_cmd), "AT+CPIN?\r\n");
rsp_num = 2;  // Need 2 response lines
for (int i = 1; i < rsp_num; i++) {
    if (rsp_list[i] == NULL) {
        rsp_list[i] = (char *)pvPortMalloc(MODEM_AT_RSP_LEN_MAXIMUM);
    }
}
ret = modem_at_cmd_wait_rsp(&modem_at_handle, at_cmd, rsp_list, rsp_num, timeout);
if (ret >= MODEM_OK) {
    for (int i = 0; i < ret; i++) {
        printf("SIM status response[%d]: %s\n", i, rsp_list[i]);
    }
} else {
    printf("AT command execution failed: %d\n", ret);
}

// Free response buffers
for (int i = 0; i < MODEM_AT_RSP_MAX_LINE_NUM; i++) {
    if (rsp_list[i] != NULL) {
        vPortFree(rsp_list[i]);
        rsp_list[i] = NULL;
    }
}
```

### 7.3 Command Line Complete Flow

```bash
# 1. Initialize
ifconfig 4g init

# 2. View information (automatically in DOWN state)
ifconfig 4g info

# 3. Set APN (optional)
ifconfig 4g cfg cmnet

# 4. Connect
ifconfig 4g up

# 5. View information after connection
ifconfig 4g info

# 6. Disconnect
ifconfig 4g down

# 7. Send AT commands
modem AT+CSQ
modem AT+CPIN?
modem AT+GSN

# 8. Reconnect
ifconfig 4g up
```

## 8. State Descriptions

### 8.1 Network Card States

- `NETIF_STATE_DEINIT`: Not initialized
- `NETIF_STATE_DOWN`: Initialized but not connected (can get latest device information/configuration, can send AT commands, but IP address and other information may be empty or incorrect)
- `NETIF_STATE_UP`: Connected (can use network normally, IP address and other information are correct, but device information is cached data)

### 8.2 Modem States

- `MODEM_STATE_UNINIT`: Not initialized
- `MODEM_STATE_INIT`: Initialized (can send AT commands)
- `MODEM_STATE_PPP`: PPP mode (connected)

## 9. Error Handling

### 9.1 Common Error Codes

- `AICAM_OK (0)`: Success
- `AICAM_ERROR_INVALID_PARAM`: Invalid parameter
- `AICAM_ERROR_BUSY`: Device busy (possible reasons: device not initialized, device already in target state, device in use)
- `AICAM_ERROR_TIMEOUT`: Timeout
- `AICAM_ERROR_NOT_INITIALIZED`: Not initialized
- `MODEM_ERR_INVALID_STATE`: Modem state error
- `MODEM_ERR_TIMEOUT`: Modem operation timeout

### 9.2 Error Handling Example

```c
int ret = nm_ctrl_netif_up("4g");
if (ret != 0) {
    switch (ret) {
        case AICAM_ERROR_BUSY:
            // Possible reasons: device not initialized, device already in UP state, device in use
            netif_state_t state = nm_get_netif_state("4g");
            if (state == NETIF_STATE_DEINIT) {
                printf("Network card not initialized, please initialize first\n");
            } else if (state == NETIF_STATE_UP) {
                printf("Network card is already in UP state\n");
            } else {
                printf("Network card is in use, please disconnect first\n");
            }
            break;
        case AICAM_ERROR_TIMEOUT:
            printf("Connection timeout, please check SIM card and signal\n");
            break;
        case AICAM_ERROR_NOT_INITIALIZED:
            printf("Network card not initialized, please initialize first\n");
            break;
        default:
            printf("Connection failed: %d\n", ret);
            break;
    }
}
```

## 10. Notes

1. **State Checking**: Before executing any operation, it is recommended to check the network card state first
2. **Information Retrieval Timing**:
   - **Network Information** (IP address, gateway, subnet mask): Only correct when in **UP** state (automatically obtained through PPP negotiation, 4G network card does not support configuring these parameters)
   - **Device Information** (IMEI, IMSI, ICCID, signal strength, SIM card status, etc.): Latest data can only be obtained when in **DOWN** state
   - When in **UP** state, device information is cached data from initialization or the last **DOWN** state
3. **IP Address Configuration**: 4G network card does not support configuring IP address, gateway, subnet mask and other network parameters. These parameters are automatically obtained through PPP negotiation
4. **Set Configuration Timing**: When using the `nm_set_netif_cfg()` function, **no manual state checking or calling down/up is required**. The function internally handles the down->cfg->up flow automatically. If the network card is currently in UP state, it will automatically restore UP state after configuration
5. **Configuration Update**: After modifying configuration with `nm_set_netif_cfg()`, if the network card was previously in UP state, the configuration will automatically take effect (function internally automatically ups)
6. **Connection Wait**: The `nm_ctrl_netif_up()` function internally waits for network card ready, including SIM card ready, PPP connection establishment, etc. No additional delay is needed
7. **AT Command Restrictions**: When sending AT commands, the network card must be in DOWN state, and the modem must be in INIT state
8. **SIM Card Status**: Before connecting, ensure the SIM card is ready (READY state). `nm_ctrl_netif_up()` internally waits for SIM card ready (up to 10 seconds)
9. **Signal Strength**: Before connecting, it is recommended to check signal strength. Weak signal may cause connection failure
10. **Timeout Settings**: Adjust AT command timeout appropriately according to command complexity

## 11. Related API Reference

### 11.1 Network Card Management API

- `nm_ctrl_netif_init()`: Initialize network card
- `nm_ctrl_netif_up()`: Start/connect network card
- `nm_ctrl_netif_down()`: Stop/disconnect network card
- `nm_ctrl_netif_deinit()`: Deinitialize network card
- `nm_get_netif_state()`: Get network card state
- `nm_get_netif_info()`: Get network card information
- `nm_get_netif_cfg()`: Get network card configuration
- `nm_set_netif_cfg()`: Set network card configuration
- `nm_print_netif_info()`: Print network card information

### 11.2 Modem API

- `modem_device_init()`: Initialize modem
- `modem_device_deinit()`: Deinitialize modem
- `modem_device_get_state()`: Get modem state
- `modem_device_get_info()`: Get modem information
- `modem_device_get_config()`: Get modem configuration
- `modem_device_set_config()`: Set modem configuration
- `modem_device_into_ppp()`: Enter PPP mode
- `modem_device_exit_ppp()`: Exit PPP mode

### 11.3 AT Command API

- `modem_at_cmd_wait_ok()`: Send AT command and wait for OK response
- `modem_at_cmd_wait_str()`: Send AT command and wait for specified string
- `modem_at_cmd_wait_rsp()`: Send AT command and wait for multiple line responses

## 12. Troubleshooting

### 12.1 Initialization Failure

- Check hardware connections
- Check power supply
- Check if UART configuration is correct

### 12.2 Connection Failure

- Check if SIM card is inserted and status is READY
- Check signal strength (CSQ value)
- Check if APN configuration is correct
- Check if carrier network is available

### 12.3 AT Command No Response

- Ensure network card is in DOWN state
- Ensure modem is in INIT state
- Check if AT command format is correct (needs to include \r\n)
- Appropriately increase timeout

### 12.4 Information Retrieval Inaccurate

- **Network Information** (IP address, gateway, subnet mask): Only correct when in UP state (automatically obtained through PPP negotiation). In DOWN state, may be empty or incorrect
- **Device Information** (signal strength, SIM card status, etc.): In UP state, cached data is retrieved, which may not be latest
- If you need to get latest device information, ensure retrieval in DOWN state
- If you need to get network information, ensure retrieval in UP state

---

**Document Version**: 1.0  
**Last Updated**: 2024
