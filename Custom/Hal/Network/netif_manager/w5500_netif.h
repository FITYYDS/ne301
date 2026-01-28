#ifndef __W5500_NETIF_H__
#define __W5500_NETIF_H__

#ifdef __cplusplus
    extern "C" {
#endif

#include "netif_manager.h"

int w5500_netif_init(void);
int w5500_netif_up(void);
int w5500_netif_down(void);
void w5500_netif_deinit(void);
int w5500_netif_config(netif_config_t *netif_cfg);
int w5500_netif_info(netif_info_t *netif_info);
netif_state_t w5500_netif_state(void);
struct netif *w5500_netif_ptr(void);
int w5500_netif_reset_test(void);

int w5500_netif_ctrl(const char *if_name, netif_cmd_t cmd, void *param);

#ifdef __cplusplus
}
#endif

#endif /* __W5500_NETIF_H__ */
