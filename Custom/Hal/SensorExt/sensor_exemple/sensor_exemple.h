/**
 * @file sensor_exemple.h
 * @brief Sensor example: camera pipe2 -> TFT display (command start/stop).
 */

#ifndef SENSOR_EXEMPLE_H
#define SENSOR_EXEMPLE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register debug commands for the sensor example.
 *
 * Commands:
 *   sexp start   - Init TFT, get pipe2 params (app may already have started pipe2), run preview thread
 *   sexp stop    - Stop thread and release resources (does not stop camera/pipe2)
 *   als init     - Init LTR-310/LTR-311 ALS sensor on I2C port 1, addr 0x22
 *   als read     - Read ALS and IR counts
 *   als deinit   - Deinit sensor and release I2C
 *   als reset    - Software reset IC
 *   als id       - Read PART_ID and MANUFAC_ID
 *   als status   - Read ALS_STATUS
 *   als thresh <hi> <lo>   - Set interrupt thresholds
 *   als timing <scale> <int_steps> <mrr_steps> - Set timing registers
 *   als avg <val>          - Set ALS averaging (reg 0x7F)
 *   mlx90642 init [addr]      - Init MLX90642 IR array sensor on I2C port 1, addr 0x66
 *   mlx90642 measure          - One sync measurement, print min/max/avg/center pixel temp
 *   mlx90642 dump             - Measure and dump all 768 pixel temps (centi-C, 32x24 grid)
 *   mlx90642 id               - Read 64-bit unique device ID
 *   mlx90642 version          - Read firmware version
 *   mlx90642 rate <2|4|8|16|32> - Set refresh rate in Hz
 *   mlx90642 rate_r           - Read current refresh rate
 *   mlx90642 emissivity <val> - Set emissivity (0x0001..0x4000, 0x4000=1.0)
 *   mlx90642 emissivity_r     - Read current emissivity
 *   mlx90642 treflected <val> - Set reflected temp (centi-C, e.g. 2500=25.00C)
 *   mlx90642 mode <cont|step> - Set measurement mode
 *   mlx90642 mode_r           - Read measurement mode
 *   mlx90642 format <temp|norm> - Set output format (temperature or normalized)
 *   mlx90642 format_r         - Read output format
 *   mlx90642 status           - Read data-ready / busy / read-window flags
 *   mlx90642 sleep            - Send sensor to sleep
 *   mlx90642 wakeup           - Wake sensor from sleep
 *   mlx90642 deinit           - Deinit sensor and release I2C instance
 *   dts6012m init [addr]      - Init DTS6012M TOF LIDAR on I2C port 1, addr 0x51, laser on
 *   dts6012m read             - Read distance in mm and m
 *   dts6012m laser <0|1>      - Turn laser off (0) or on (1)
 *   dts6012m deinit           - Turn laser off, release I2C instance
 */
void sensor_exemple_register_commands(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_EXEMPLE_H */
