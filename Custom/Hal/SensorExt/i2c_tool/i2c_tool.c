#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "../i2c_driver/i2c_driver.h"
#include "i2c_tool.h"

/**
 * @brief Print usage for i2c_tool command.
 */
static void i2c_tool_print_usage(void)
{
    LOG_SIMPLE("Usage:");
    LOG_SIMPLE("  i2c_tool detect [bus] [start_addr] [end_addr]");
    LOG_SIMPLE("    bus        : I2C bus number, default 1");
    LOG_SIMPLE("    start_addr : start 7-bit address in hex, default 0x03");
    LOG_SIMPLE("    end_addr   : end 7-bit address in hex, default 0x77");
}

/**
 * @brief Perform an i2c bus scan similar to `i2cdetect -y`.
 *
 * Only I2C_PORT_1 is currently supported. The address range is 7-bit
 * address (0x00 - 0x7F).
 */
static int i2c_tool_cmd_detect(int bus, int start_addr, int end_addr)
{
    int ret = 0;
    i2c_port_t port = I2C_PORT_1;

    /* Currently only port 1 is supported. */
    if (bus != 1) {
        LOG_SIMPLE("Warning: only bus 1 is supported, using bus 1");
    }

    if (start_addr < 0) start_addr = 0;
    if (end_addr > 0x7F) end_addr = 0x7F;
    if (start_addr > end_addr) {
        LOG_SIMPLE("Invalid range: start_addr > end_addr");
        return -1;
    }

    ret = i2c_driver_init(port);
    if (ret != 0) {
        LOG_SIMPLE("i2c_driver_init failed: %d", ret);
        return ret;
    }

    LOG_SIMPLE("Scanning I2C bus %d, address range 0x%02X-0x%02X", bus, start_addr, end_addr);

    /* Print header line similar to linux i2cdetect. */
    {
        char header[64] = {0};
        int pos = 0;
        pos += snprintf(header + pos, sizeof(header) - (size_t)pos, "     ");
        for (int col = 0; col < 16 && pos < (int)sizeof(header); col++) {
            pos += snprintf(header + pos, sizeof(header) - (size_t)pos, " %02x", col);
        }
        LOG_SIMPLE("%s", header);
    }

    for (int base = 0; base < 0x80; base += 0x10) {
        char line[128] = {0};
        int pos = 0;

        pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "%02x: ", base);

        for (int offset = 0; offset < 0x10; offset++) {
            int addr = base + offset;

            if (addr < start_addr || addr > end_addr) {
                /* Out of scan range, print placeholder. */
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "   ");
                continue;
            }

            /* 7-bit address to 8-bit HAL address. */
            uint16_t dev_addr = (uint16_t)(addr << 1);
            i2c_instance_t *instance = i2c_driver_create(port, dev_addr, I2C_ADDRESS_7BIT);
            if (instance == NULL) {
                /* Cannot create instance, print placeholder. */
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "?? ");
                continue;
            }

            ret = i2c_driver_is_ready(instance, 3, 100);
            if (ret == 0) {
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "%02x ", addr);
            } else {
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "-- ");
            }

            i2c_driver_destroy(instance);
        }

        LOG_SIMPLE("%s", line);
    }

    return 0;
}

/**
 * @brief Main command handler for i2c_tool.
 */
static int i2c_tool_cmd(int argc, char *argv[])
{
    if (argc < 2) {
        i2c_tool_print_usage();
        return -1;
    }

    if (strcmp(argv[1], "detect") == 0) {
        int bus = 1;
        int start_addr = 0x03;
        int end_addr = 0x77;

        if (argc >= 3) {
            bus = atoi(argv[2]);
        }
        if (argc >= 4) {
            /* Support hex or decimal input. */
            start_addr = (int)strtol(argv[3], NULL, 0);
        }
        if (argc >= 5) {
            end_addr = (int)strtol(argv[4], NULL, 0);
        }

        return i2c_tool_cmd_detect(bus, start_addr, end_addr);
    }

    i2c_tool_print_usage();
    return -1;
}

/* Command table for i2c_tool. */
static debug_cmd_reg_t i2c_tool_cmd_table[] = {
    {"i2c_tool", "I2C debug tool similar to linux i2c-tools", i2c_tool_cmd},
};

/* Internal register function, used by driver_cmd_register_callback. */
static void i2c_tool_cmd_register(void)
{
    debug_cmdline_register(i2c_tool_cmd_table, (int)(sizeof(i2c_tool_cmd_table) / sizeof(i2c_tool_cmd_table[0])));
}

void i2c_tool_register(void)
{
    driver_cmd_register_callback("i2c_tool", i2c_tool_cmd_register);
}

