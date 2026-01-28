#ifndef WIFI_H
#define WIFI_H

#include "cmsis_os2.h"
#include "dev_manager.h"
#include "mem_map.h"
#include "pwr.h"

#define WIFI_FIR_NAME "siwg917"

#define WIFI_MODE_UPDATE            "update"
#define WIFI_MODE_NORMAL            "normal"
#define WIFI_MODE_TX_TEST           "tx_test"

#define NVS_KEY_WIFI_MODE           "wifi_mode"
#define NVS_KEY_WIFI_UPDATE_TIMES   "wifi_update_times"
//! Type of FW update
#define M4_FW_UPDATE  0 // Only Supported for SoC
#define NWP_FW_UPDATE 1

//! Set FW update type
#define FW_UPDATE_TYPE NWP_FW_UPDATE

#define SI91X_CHUNK_SIZE      4096UL
#define SI91X_IN_BETWEEN_FILE 0UL
#define SI91X_START_OF_FILE   1UL
#define SI91X_END_OF_FILE     2UL
#define SI91X_FW_VER_SIZE     20UL
#define FW_HEADER_SIZE        64UL
#define XMODEM_CHUNK_SIZE     128UL
#define FIRST_PKT_XMODEM_CNT  32UL

#define WIFI_FLASH_BASE_ADDR       WIFI_FW_BASE
#define WIFI_FLASH_HEADER_SIZE     32UL
#define WIFI_FLASH_VALID_FLAGS     0x20060123UL

typedef enum si91x_wlan_app_state_e {
  SI91X_WLAN_INITIAL_STATE    = 0,
  SI91X_WLAN_RADIO_INIT_STATE = 1,
  SI91X_WLAN_FW_UPGRADE       = 2,
  SI91X_WLAN_FW_UPGRADE_DONE  = 3
} si91x_wlan_app_state_t;

#pragma pack(push, 1)
typedef struct {
  uint32_t valid_flags;
  uint32_t fw_total_size;
  uint32_t fw_crc;
  uint32_t reserved[5];
} flash_header_t;
typedef struct fwupeq_s {
  uint16_t control_flags;
  uint16_t sha_type;
  uint32_t magic_no;
  uint32_t image_size;
  uint32_t fw_version;
  uint32_t flash_loc;
  uint32_t crc;
} fwreq_t;
#pragma pack(pop)

typedef struct si91x_wlan_app_cb_s {
  //! wlan application state
  si91x_wlan_app_state_t state;
  //! length of buffer to copy
  uint32_t length;
  //! to check application buffer availability
  uint8_t buf_in_use;
  //! application events bit map
  uint32_t event_map;

} si91x_wlan_app_cb_t;


/*
 * @brief Process wifi mode
 */
void wifi_mode_process(void);

int is_wifi_ant(void);

int is_wifi_update(void);

void wifi_enter_update_mode(void);

uint32_t get_wifi_update_times(void);

void wifi_register(void);
#endif