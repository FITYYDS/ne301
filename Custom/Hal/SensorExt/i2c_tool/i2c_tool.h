/**
 * @file i2c_tool.h
 * @brief Simple I2C debug tools similar to linux i2c-tools.
 */

#ifndef __I2C_TOOL_H__
#define __I2C_TOOL_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register I2C debug commands.
 *
 * This will register the `i2c_tool` command into the debug
 * command line system via `driver_cmd_register_callback`.
 */
void i2c_tool_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __I2C_TOOL_H__ */

