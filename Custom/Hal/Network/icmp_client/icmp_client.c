#include <string.h>
#include <stdio.h>
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "ping.h"
#include "Log/debug.h"
#include "icmp_client.h"

static ip_addr_t target;

void icmp_client_dns_found(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    if (ipaddr != NULL) {
        LOG_DRV_INFO("DNS found: %s -> %s", name, ipaddr_ntoa(ipaddr));
        target.addr = ipaddr->addr;
        ping_init(&target);
    } else {
        LOG_DRV_ERROR("DNS not found: %s", name);
    }
}

int icmp_client_cmd_deal(int argc, char* argv[])
{
    err_t err = ERR_OK;

    if (argc < 2) {
        LOG_SIMPLE("Usage: ping [ip/host]\r\n");
        return -1;
    }

    if (ipaddr_aton(argv[1], &target)) {
        ping_init(&target);
    } else {
        err = dns_gethostbyname(argv[1], &target, icmp_client_dns_found, NULL);
        if (err == ERR_OK) {
            ping_init(&target);
        } else if (err != ERR_INPROGRESS) {
            LOG_DRV_ERROR("DNS query failed(ret = %d)!", err);
        }
    }
    return err;
}

debug_cmd_reg_t icmp_client_cmd_table[] = {
    {"ping",    "ping network addr.",      icmp_client_cmd_deal},
};

static void icmp_client_cmd_register(void)
{
    debug_cmdline_register(icmp_client_cmd_table, sizeof(icmp_client_cmd_table) / sizeof(icmp_client_cmd_table[0]));
}

void icmp_client_register(void)
{
    driver_cmd_register_callback("icmp", icmp_client_cmd_register);
}
