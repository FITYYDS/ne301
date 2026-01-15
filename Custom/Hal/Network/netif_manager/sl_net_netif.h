#ifndef __SL_NET_NETIF_H__
#define __SL_NET_NETIF_H__

#ifdef __cplusplus
    extern "C" {
#endif

#include "netif_manager.h"

/**************************************** WiFi Network Interface Related Interfaces *******************************************/
int sl_net_client_netif_init(void);
int sl_net_client_netif_up(void);
int sl_net_client_netif_down(void);
void sl_net_client_netif_deinit(void);
int sl_net_client_netif_config(netif_config_t *netif_cfg);
int sl_net_client_netif_info(netif_info_t *netif_info);
netif_state_t sl_net_client_netif_state(void);
struct netif *sl_net_client_netif_ptr(void);

int sl_net_ap_netif_init(void);
int sl_net_ap_netif_up(void);
int sl_net_ap_netif_down(void);
void sl_net_ap_netif_deinit(void);
int sl_net_ap_netif_config(netif_config_t *netif_cfg);
int sl_net_ap_netif_info(netif_info_t *netif_info);
netif_state_t sl_net_ap_netif_state(void);
struct netif *sl_net_ap_netif_ptr(void);

typedef enum {
    WAKEUP_MODE_NORMAL = 0,
    WAKEUP_MODE_WIFI = 1,
    WAKEUP_MODE_BLE = 2,
    WAKEUP_MODE_MAX,
} sl_net_wakeup_mode_t;

int sl_net_netif_init(void);
sl_net_wakeup_mode_t sl_net_netif_get_wakeup_mode(void);
int sl_net_netif_romote_wakeup_mode_ctrl(sl_net_wakeup_mode_t wakeup_mode);
int sl_net_netif_filter_broadcast_ctrl(uint8_t enable);
int sl_net_netif_low_power_mode_ctrl(uint8_t enable);
int sl_net_netif_ctrl(const char *if_name, netif_cmd_t cmd, void *param);
int sl_net_start_scan(wireless_scan_callback_t callback);
wireless_scan_result_t *sl_net_get_strorage_scan_result(void);
int sl_net_update_strorage_scan_result(uint32_t timeout_ms);

void sli_firmware_error_callback(int error_code);
/***************************************************************************************************/

#ifdef __cplusplus
}
#endif

#endif /* __SL_NET_NETIF_H__ */