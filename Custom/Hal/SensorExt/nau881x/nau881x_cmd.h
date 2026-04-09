/**
 * @file nau881x_cmd.h
 * @brief NAU881x debug CLI command registration.
 *
 * Commands registered under the "nau881x" keyword:
 *   nau881x init
 *   nau881x deinit
 *   nau881x rev
 *   nau881x pga_input <none|micp|micn|aux>
 *   nau881x pga_gain  <0..63>
 *   nau881x pga_gain_db <dB>
 *   nau881x pga_mute  <0|1>
 *   nau881x pga_boost <0|1>
 *   nau881x pga_en    <0|1>
 *   nau881x micbias_en <0|1>
 *   nau881x adc_en    <0|1>
 *   nau881x adc_gain  <0..255>
 *   nau881x adc_hpf   <en> <mode> <freq>
 *   nau881x boost_en  <0|1>
 *   nau881x alc_en    <0|1>
 *   nau881x alc_gain  <min> <max>
 *   nau881x alc_target <0..15>
 *   nau881x alc_mode  <normal|limiter>
 *   nau881x ng_en     <0|1>
 *   nau881x ng_thresh <0..7>
 *   nau881x dac_en    <0|1>
 *   nau881x dac_gain  <0..255>
 *   nau881x dac_mute  <0|1>
 *   nau881x dac_passthrough <0|1>
 *   nau881x spk_en    <0|1>
 *   nau881x spk_vol   <0..63>
 *   nau881x spk_vol_db <-57..5>
 *   nau881x spk_mute  <0|1>
 *   nau881x spk_src   <dac|bypass>
 *   nau881x clock <master> <bclkdiv> <mclkdiv> <src>
 *   nau881x fmt   <rj|lj|i2s|pcma|pcmb> <16|20|24|32|8>
 *   nau881x dump
 */

#ifndef NAU881X_CMD_H
#define NAU881X_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the "nau881x" debug command.
 */
void nau881x_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif /* NAU881X_CMD_H */
