
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef OS04C10_REG_H
#define OS04C10_REG_H

#include <cmsis_compiler.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
/** @addtogroup BSP
  * @{
  */

/** @addtogroup Components
  * @{
  */

/** @addtogroup OS04C10
  * @{
  */

/** @defgroup OS04C10_Exported_Types
  * @{
  */

/**
  * @}
  */

/** @defgroup OS04C10_Exported_Constants OS04C10 Exported Constants
  * @{
  */

#define OS04C10_MAX_A_GAIN          15872//(15.5*1024)
#define OS04C10_SENSOR_MAX_GAIN     246*1024//(15.5*15.9)                  // max sensor again, a-gain * conversion-gain*d-gain
/**
  * @brief  OS04C10 ID
  */
#define  OS04C10_ID                0x5304U


#define OS04C10_NAME               "OS04C10"
#define OS04C10_BAYER_PATTERN      0 /* From ISP definition RGGB / TODO comnon enumeration in camera */
#define OS04C10_COLOR_DEPTH        10 /* in bits */
#define OS04C10_GAIN_MIN           (0 * 1000)
#define OS04C10_GAIN_MAX           (72 * 1000)
#define OS04C10_GAIN_DEFAULT       (20 * 1000)
#define OS04C10_GAIN_UNIT_MDB      300
#define OS04C10_EXPOSURE_MIN       0           /* in us */
#define OS04C10_EXPOSURE_MAX       33266       /* in us, for sensor @30fps */

// /* For 2688x1520 */
#define OS04C10_WIDTH              2688
#define OS04C10_HEIGHT             1520

// /* For 1920x1080 */
// #define OS04C10_WIDTH              1920
// #define OS04C10_HEIGHT             1080

/**
  * @brief  OS04C10 Registers
  */
/* system and IO pad control [0x3000 ~ 0x3052]        */
#define OS04C10_SYSREM_RESET00                      0x3000U
#define OS04C10_SYSREM_RESET01                      0x3001U
#define OS04C10_SYSREM_RESET02                      0x3002U
#define OS04C10_SYSREM_RESET03                      0x3003U
#define OS04C10_CLOCK_ENABLE00                      0x3004U
#define OS04C10_CLOCK_ENABLE01                      0x3005U
#define OS04C10_CLOCK_ENABLE02                      0x3006U
#define OS04C10_CLOCK_ENABLE03                      0x3007U
#define OS04C10_SYSTEM_CTROL0                       0x3008U
#define OS04C10_CHIP_ID_HIGH_BYTE                   0x300AU
#define OS04C10_CHIP_ID_LOW_BYTE                    0x300BU
#define OS04C10_MIPI_CONTROL00                      0x300EU
#define OS04C10_PAD_OUTPUT_ENABLE00                 0x3016U
#define OS04C10_PAD_OUTPUT_ENABLE01                 0x3017U
#define OS04C10_PAD_OUTPUT_ENABLE02                 0x3018U
#define OS04C10_PAD_OUTPUT_VALUE00                  0x3019U
#define OS04C10_PAD_OUTPUT_VALUE01                  0x301AU
#define OS04C10_PAD_OUTPUT_VALUE02                  0x301BU
#define OS04C10_PAD_SELECT00                        0x301CU
#define OS04C10_PAD_SELECT01                        0x301DU
#define OS04C10_PAD_SELECT02                        0x301EU
#define OS04C10_CHIP_REVISION                       0x302AU
#define OS04C10_PAD_CONTROL00                       0x301CU
#define OS04C10_SC_PWC                              0x3031U
#define OS04C10_SC_PLL_CONTRL0                      0x3034U
#define OS04C10_SC_PLL_CONTRL1                      0x3035U
#define OS04C10_SC_PLL_CONTRL2                      0x3036U
#define OS04C10_SC_PLL_CONTRL3                      0x3037U
#define OS04C10_SC_PLL_CONTRL4                      0x3038U
#define OS04C10_SC_PLL_CONTRL5                      0x3039U
#define OS04C10_SC_PLLS_CTRL0                       0x303AU
#define OS04C10_SC_PLLS_CTRL1                       0x303BU
#define OS04C10_SC_PLLS_CTRL2                       0x303CU
#define OS04C10_SC_PLLS_CTRL3                       0x303DU
#define OS04C10_IO_PAD_VALUE00                      0x3050U
#define OS04C10_IO_PAD_VALUE01                      0x3051U
#define OS04C10_IO_PAD_VALUE02                      0x3052U

/* SCCB control [0x3100 ~ 0x3108]                       */
#define OS04C10_SCCB_ID                             0x3100U
#define OS04C10_SCCB_SYSTEM_CTRL0                   0x3102U
#define OS04C10_SCCB_SYSTEM_CTRL1                   0x3103U
#define OS04C10_SYSTEM_ROOT_DIVIDER                 0x3108U

/* SRB control [0x3200 ~ 0x3213]                        */
#define OS04C10_GROUP_ADDR0                         0x3200U
#define OS04C10_GROUP_ADDR1                         0x3201U
#define OS04C10_GROUP_ADDR2                         0x3202U
#define OS04C10_GROUP_ADDR3                         0x3203U
#define OS04C10_SRM_GROUP_ACCESS                    0x3212U
#define OS04C10_SRM_GROUP_STATUS                    0x3213U

/* AWB gain control [0x3400 ~ 0x3406]                   */
#define OS04C10_AWB_R_GAIN_MSB                      0x3400U
#define OS04C10_AWB_R_GAIN_LSB                      0x3401U
#define OS04C10_AWB_G_GAIN_MSB                      0x3402U
#define OS04C10_AWB_G_GAIN_LSB                      0x3403U
#define OS04C10_AWB_B_GAIN_MSB                      0x3404U
#define OS04C10_AWB_B_GAIN_LSB                      0x3405U
#define OS04C10_AWB_MANUAL_CONTROL                  0x3406U

/* AEC/AGC control [0x3500 ~ 0x350D]                    */
#define OS04C10_AEC_PK_EXPOSURE_19_16               0x3500U
#define OS04C10_AEC_PK_EXPOSURE_HIGH                0x3501U
#define OS04C10_AEC_PK_EXPOSURE_LOW                 0x3502U
#define OS04C10_AEC_PK_MANUAL                       0x3503U
#define OS04C10_AEC_PK_REAL_GAIN_9_8                0x350AU
#define OS04C10_AEC_PK_REAL_GAIN_LOW                0x350BU
#define OS04C10_AEC_PK_VTS_HIGH                     0x350CU
#define OS04C10_AEC_PK_VTS_LOW                      0x350DU

/* VCM control [0x3600 ~ 0x3606]                        */
#define OS04C10_VCM_CONTROL_0                       0x3602U
#define OS04C10_VCM_CONTROL_1                       0x3603U
#define OS04C10_VCM_CONTROL_2                       0x3604U
#define OS04C10_VCM_CONTROL_3                       0x3605U
#define OS04C10_VCM_CONTROL_4                       0x3606U

/* timing control [0x3800 ~ 0x3821]                    */
#define OS04C10_TIMING_HS_HIGH                      0x3800U
#define OS04C10_TIMING_HS_LOW                       0x3801U
#define OS04C10_TIMING_VS_HIGH                      0x3802U
#define OS04C10_TIMING_VS_LOW                       0x3803U
#define OS04C10_TIMING_HW_HIGH                      0x3804U
#define OS04C10_TIMING_HW_LOW                       0x3805U
#define OS04C10_TIMING_VH_HIGH                      0x3806U
#define OS04C10_TIMING_VH_LOW                       0x3807U
#define OS04C10_TIMING_DVPHO_HIGH                   0x3808U
#define OS04C10_TIMING_DVPHO_LOW                    0x3809U
#define OS04C10_TIMING_DVPVO_HIGH                   0x380AU
#define OS04C10_TIMING_DVPVO_LOW                    0x380BU
#define OS04C10_TIMING_HTS_HIGH                     0x380CU
#define OS04C10_TIMING_HTS_LOW                      0x380DU
#define OS04C10_TIMING_VTS_HIGH                     0x380EU
#define OS04C10_TIMING_VTS_LOW                      0x380FU
#define OS04C10_TIMING_HOFFSET_HIGH                 0x3810U
#define OS04C10_TIMING_HOFFSET_LOW                  0x3811U
#define OS04C10_TIMING_VOFFSET_HIGH                 0x3812U
#define OS04C10_TIMING_VOFFSET_LOW                  0x3813U
#define OS04C10_TIMING_X_INC                        0x3814U
#define OS04C10_TIMING_Y_INC                        0x3815U
#define OS04C10_HSYNC_START_HIGH                    0x3816U
#define OS04C10_HSYNC_START_LOW                     0x3817U
#define OS04C10_HSYNC_WIDTH_HIGH                    0x3818U
#define OS04C10_HSYNC_WIDTH_LOW                     0x3819U
#define OS04C10_TIMING_TC_REG20                     0x3820U
#define OS04C10_TIMING_TC_REG21                     0x3821U

/* AEC/AGC power down domain control [0x3A00 ~ 0x3A25] */
#define OS04C10_AEC_CTRL00                          0x3A00U
#define OS04C10_AEC_CTRL01                          0x3A01U
#define OS04C10_AEC_CTRL02                          0x3A02U
#define OS04C10_AEC_CTRL03                          0x3A03U
#define OS04C10_AEC_CTRL04                          0x3A04U
#define OS04C10_AEC_CTRL05                          0x3A05U
#define OS04C10_AEC_CTRL06                          0x3A06U
#define OS04C10_AEC_CTRL07                          0x3A07U
#define OS04C10_AEC_B50_STEP_HIGH                   0x3A08U
#define OS04C10_AEC_B50_STEP_LOW                    0x3A09U
#define OS04C10_AEC_B60_STEP_HIGH                   0x3A0AU
#define OS04C10_AEC_B60_STEP_LOW                    0x3A0BU
#define OS04C10_AEC_AEC_CTRL0C                      0x3A0CU
#define OS04C10_AEC_CTRL0D                          0x3A0DU
#define OS04C10_AEC_CTRL0E                          0x3A0EU
#define OS04C10_AEC_CTRL0F                          0x3A0FU
#define OS04C10_AEC_CTRL10                          0x3A10U
#define OS04C10_AEC_CTRL11                          0x3A11U
#define OS04C10_AEC_CTRL13                          0x3A13U
#define OS04C10_AEC_MAX_EXPO_HIGH                   0x3A14U
#define OS04C10_AEC_MAX_EXPO_LOW                    0x3A15U
#define OS04C10_AEC_CTRL17                          0x3A17U
#define OS04C10_AEC_GAIN_CEILING_HIGH               0x3A18U
#define OS04C10_AEC_GAIN_CEILING_LOW                0x3A19U
#define OS04C10_AEC_DIFF_MIN                        0x3A1AU
#define OS04C10_AEC_CTRL1B                          0x3A1BU
#define OS04C10_LED_ADD_ROW_HIGH                    0x3A1CU
#define OS04C10_LED_ADD_ROW_LOW                     0x3A1DU
#define OS04C10_AEC_CTRL1E                          0x3A1EU
#define OS04C10_AEC_CTRL1F                          0x3A1FU
#define OS04C10_AEC_CTRL20                          0x3A20U
#define OS04C10_AEC_CTRL21                          0x3A21U
#define OS04C10_AEC_CTRL25                          0x3A25U

/* strobe control [0x3B00 ~ 0x3B0C]                      */
#define OS04C10_STROBE_CTRL                         0x3B00U
#define OS04C10_FREX_EXPOSURE02                     0x3B01U
#define OS04C10_FREX_SHUTTER_DLY01                  0x3B02U
#define OS04C10_FREX_SHUTTER_DLY00                  0x3B03U
#define OS04C10_FREX_EXPOSURE01                     0x3B04U
#define OS04C10_FREX_EXPOSURE00                     0x3B05U
#define OS04C10_FREX_CTRL07                         0x3B06U
#define OS04C10_FREX_MODE                           0x3B07U
#define OS04C10_FREX_RQST                           0x3B08U
#define OS04C10_FREX_HREF_DLY                       0x3B09U
#define OS04C10_FREX_RST_LENGTH                     0x3B0AU
#define OS04C10_STROBE_WIDTH_HIGH                   0x3B0BU
#define OS04C10_STROBE_WIDTH_LOW                    0x3B0CU

/* 50/60Hz detector control [0x3C00 ~ 0x3C1E]            */
#define OS04C10_5060HZ_CTRL00                       0x3C00U
#define OS04C10_5060HZ_CTRL01                       0x3C01U
#define OS04C10_5060HZ_CTRL02                       0x3C02U
#define OS04C10_5060HZ_CTRL03                       0x3C03U
#define OS04C10_5060HZ_CTRL04                       0x3C04U
#define OS04C10_5060HZ_CTRL05                       0x3C05U
#define OS04C10_LIGHTMETER1_TH_HIGH                 0x3C06U
#define OS04C10_LIGHTMETER1_TH_LOW                  0x3C07U
#define OS04C10_LIGHTMETER2_TH_HIGH                 0x3C08U
#define OS04C10_LIGHTMETER2_TH_LOW                  0x3C09U
#define OS04C10_SAMPLE_NUMBER_HIGH                  0x3C0AU
#define OS04C10_SAMPLE_NUMBER_LOW                   0x3C0BU
#define OS04C10_SIGMA_DELTA_CTRL0C                  0x3C0CU
#define OS04C10_SUM50_BYTE4                         0x3C0DU
#define OS04C10_SUM50_BYTE3                         0x3C0EU
#define OS04C10_SUM50_BYTE2                         0x3C0FU
#define OS04C10_SUM50_BYTE1                         0x3C10U
#define OS04C10_SUM60_BYTE4                         0x3C11U
#define OS04C10_SUM60_BYTE3                         0x3C12U
#define OS04C10_SUM60_BYTE2                         0x3C13U
#define OS04C10_SUM60_BYTE1                         0x3C14U
#define OS04C10_SUM5060_HIGH                        0x3C15U
#define OS04C10_SUM5060_LOW                         0x3C16U
#define OS04C10_BLOCK_CNTR_HIGH                     0x3C17U
#define OS04C10_BLOCK_CNTR_LOW                      0x3C18U
#define OS04C10_B6_HIGH                             0x3C19U
#define OS04C10_B6_LOW                              0x3C1AU
#define OS04C10_LIGHTMETER_OUTPUT_BYTE3             0x3C1BU
#define OS04C10_LIGHTMETER_OUTPUT_BYTE2             0x3C1CU
#define OS04C10_LIGHTMETER_OUTPUT_BYTE1             0x3C1DU
#define OS04C10_SUM_THRESHOLD                       0x3C1EU

/* OTP control [0x3D00 ~ 0x3D21]                         */
/* MC control [0x3F00 ~ 0x3F0D]                          */
/* BLC control [0x4000 ~ 0x4033]                         */
#define OS04C10_BLC_CTRL00                          0x4000U
#define OS04C10_BLC_CTRL01                          0x4001U
#define OS04C10_BLC_CTRL02                          0x4002U
#define OS04C10_BLC_CTRL03                          0x4003U
#define OS04C10_BLC_CTRL04                          0x4004U
#define OS04C10_BLC_CTRL05                          0x4005U

/* frame control [0x4201 ~ 0x4202]                       */
#define OS04C10_FRAME_CTRL01                        0x4201U
#define OS04C10_FRAME_CTRL02                        0x4202U

/* format control [0x4300 ~ 0x430D]                      */
#define OS04C10_FORMAT_CTRL00                       0x4300U
#define OS04C10_FORMAT_CTRL01                       0x4301U
#define OS04C10_YMAX_VAL_HIGH                       0x4302U
#define OS04C10_YMAX_VAL_LOW                        0x4303U
#define OS04C10_YMIN_VAL_HIGH                       0x4304U
#define OS04C10_YMIN_VAL_LOW                        0x4305U
#define OS04C10_UMAX_VAL_HIGH                       0x4306U
#define OS04C10_UMAX_VAL_LOW                        0x4307U
#define OS04C10_UMIN_VAL_HIGH                       0x4308U
#define OS04C10_UMIN_VAL_LOW                        0x4309U
#define OS04C10_VMAX_VAL_HIGH                       0x430AU
#define OS04C10_VMAX_VAL_LOW                        0x430BU
#define OS04C10_VMIN_VAL_HIGH                       0x430CU
#define OS04C10_VMIN_VAL_LOW                        0x430DU

/* JPEG control [0x4400 ~ 0x4431]                        */
#define OS04C10_JPEG_CTRL00                         0x4400U
#define OS04C10_JPEG_CTRL01                         0x4401U
#define OS04C10_JPEG_CTRL02                         0x4402U
#define OS04C10_JPEG_CTRL03                         0x4403U
#define OS04C10_JPEG_CTRL04                         0x4404U
#define OS04C10_JPEG_CTRL05                         0x4405U
#define OS04C10_JPEG_CTRL06                         0x4406U
#define OS04C10_JPEG_CTRL07                         0x4407U
#define OS04C10_JPEG_ISI_CTRL1                      0x4408U
#define OS04C10_JPEG_CTRL09                         0x4409U
#define OS04C10_JPEG_CTRL0A                         0x440AU
#define OS04C10_JPEG_CTRL0B                         0x440BU
#define OS04C10_JPEG_CTRL0C                         0x440CU
#define OS04C10_JPEG_QT_DATA                        0x4410U
#define OS04C10_JPEG_QT_ADDR                        0x4411U
#define OS04C10_JPEG_ISI_DATA                       0x4412U
#define OS04C10_JPEG_ISI_CTRL2                      0x4413U
#define OS04C10_JPEG_LENGTH_BYTE3                   0x4414U
#define OS04C10_JPEG_LENGTH_BYTE2                   0x4415U
#define OS04C10_JPEG_LENGTH_BYTE1                   0x4416U
#define OS04C10_JFIFO_OVERFLOW                      0x4417U

/* VFIFO control [0x4600 ~ 0x460D]                       */
#define OS04C10_VFIFO_CTRL00                        0x4600U
#define OS04C10_VFIFO_HSIZE_HIGH                    0x4602U
#define OS04C10_VFIFO_HSIZE_LOW                     0x4603U
#define OS04C10_VFIFO_VSIZE_HIGH                    0x4604U
#define OS04C10_VFIFO_VSIZE_LOW                     0x4605U
#define OS04C10_VFIFO_CTRL0C                        0x460CU
#define OS04C10_VFIFO_CTRL0D                        0x460DU

/* DVP control [0x4709 ~ 0x4745]                         */
#define OS04C10_DVP_VSYNC_WIDTH0                    0x4709U
#define OS04C10_DVP_VSYNC_WIDTH1                    0x470AU
#define OS04C10_DVP_VSYNC_WIDTH2                    0x470BU
#define OS04C10_PAD_LEFT_CTRL                       0x4711U
#define OS04C10_PAD_RIGHT_CTRL                      0x4712U
#define OS04C10_JPG_MODE_SELECT                     0x4713U
#define OS04C10_656_DUMMY_LINE                      0x4715U
#define OS04C10_CCIR656_CTRL                        0x4719U
#define OS04C10_HSYNC_CTRL00                        0x471BU
#define OS04C10_DVP_VSYN_CTRL                       0x471DU
#define OS04C10_DVP_HREF_CTRL                       0x471FU
#define OS04C10_VSTART_OFFSET                       0x4721U
#define OS04C10_VEND_OFFSET                         0x4722U
#define OS04C10_DVP_CTRL23                          0x4723U
#define OS04C10_CCIR656_CTRL00                      0x4730U
#define OS04C10_CCIR656_CTRL01                      0x4731U
#define OS04C10_CCIR656_FS                          0x4732U
#define OS04C10_CCIR656_FE                          0x4733U
#define OS04C10_CCIR656_LS                          0x4734U
#define OS04C10_CCIR656_LE                          0x4735U
#define OS04C10_CCIR656_CTRL06                      0x4736U
#define OS04C10_CCIR656_CTRL07                      0x4737U
#define OS04C10_CCIR656_CTRL08                      0x4738U
#define OS04C10_POLARITY_CTRL                       0x4740U
#define OS04C10_TEST_PATTERN                        0x4741U
#define OS04C10_DATA_ORDER                          0x4745U

/* MIPI control [0x4800 ~ 0x4837]                        */
#define OS04C10_MIPI_CTRL00                         0x4800U
#define OS04C10_MIPI_CTRL01                         0x4801U
#define OS04C10_MIPI_CTRL05                         0x4805U
#define OS04C10_MIPI_DATA_ORDER                     0x480AU
#define OS04C10_MIN_HS_ZERO_HIGH                    0x4818U
#define OS04C10_MIN_HS_ZERO_LOW                     0x4819U
#define OS04C10_MIN_MIPI_HS_TRAIL_HIGH              0x481AU
#define OS04C10_MIN_MIPI_HS_TRAIL_LOW               0x481BU
#define OS04C10_MIN_MIPI_CLK_ZERO_HIGH              0x481CU
#define OS04C10_MIN_MIPI_CLK_ZERO_LOW               0x481DU
#define OS04C10_MIN_MIPI_CLK_PREPARE_HIGH           0x481EU
#define OS04C10_MIN_MIPI_CLK_PREPARE_LOW            0x481FU
#define OS04C10_MIN_CLK_POST_HIGH                   0x4820U
#define OS04C10_MIN_CLK_POST_LOW                    0x4821U
#define OS04C10_MIN_CLK_TRAIL_HIGH                  0x4822U
#define OS04C10_MIN_CLK_TRAIL_LOW                   0x4823U
#define OS04C10_MIN_LPX_PCLK_HIGH                   0x4824U
#define OS04C10_MIN_LPX_PCLK_LOW                    0x4825U
#define OS04C10_MIN_HS_PREPARE_HIGH                 0x4826U
#define OS04C10_MIN_HS_PREPARE_LOW                  0x4827U
#define OS04C10_MIN_HS_EXIT_HIGH                    0x4828U
#define OS04C10_MIN_HS_EXIT_LOW                     0x4829U
#define OS04C10_MIN_HS_ZERO_UI                      0x482AU
#define OS04C10_MIN_HS_TRAIL_UI                     0x482BU
#define OS04C10_MIN_CLK_ZERO_UI                     0x482CU
#define OS04C10_MIN_CLK_PREPARE_UI                  0x482DU
#define OS04C10_MIN_CLK_POST_UI                     0x482EU
#define OS04C10_MIN_CLK_TRAIL_UI                    0x482FU
#define OS04C10_MIN_LPX_PCLK_UI                     0x4830U
#define OS04C10_MIN_HS_PREPARE_UI                   0x4831U
#define OS04C10_MIN_HS_EXIT_UI                      0x4832U
#define OS04C10_PCLK_PERIOD                         0x4837U

/* ISP frame control [0x4901 ~ 0x4902]                   */
#define OS04C10_ISP_FRAME_CTRL01                    0x4901U
#define OS04C10_ISP_FRAME_CTRL02                    0x4902U

/* ISP top control [0x5000 ~ 0x5063]                     */
#define OS04C10_ISP_CONTROL00                       0x5000U
#define OS04C10_ISP_CONTROL01                       0x5001U
#define OS04C10_ISP_CONTROL03                       0x5003U
#define OS04C10_ISP_CONTROL05                       0x5005U
#define OS04C10_ISP_MISC0                           0x501DU
#define OS04C10_ISP_MISC1                           0x501EU
#define OS04C10_FORMAT_MUX_CTRL                     0x501FU
#define OS04C10_DITHER_CTRL0                        0x5020U
#define OS04C10_DRAW_WINDOW_CTRL00                  0x5027U
#define OS04C10_DRAW_WINDOW_LEFT_CTRL_HIGH          0x5028U
#define OS04C10_DRAW_WINDOW_LEFT_CTRL_LOW           0x5029U
#define OS04C10_DRAW_WINDOW_RIGHT_CTRL_HIGH         0x502AU
#define OS04C10_DRAW_WINDOW_RIGHT_CTRL_LOW          0x502BU
#define OS04C10_DRAW_WINDOW_TOP_CTRL_HIGH           0x502CU
#define OS04C10_DRAW_WINDOW_TOP_CTRL_LOW            0x502DU
#define OS04C10_DRAW_WINDOW_BOTTOM_CTRL_HIGH        0x502EU
#define OS04C10_DRAW_WINDOW_BOTTOM_CTRL_LOW         0x502FU
#define OS04C10_DRAW_WINDOW_HBW_CTRL_HIGH           0x5030U          /* HBW: Horizontal Boundary Width */
#define OS04C10_DRAW_WINDOW_HBW_CTRL_LOW            0x5031U
#define OS04C10_DRAW_WINDOW_VBW_CTRL_HIGH           0x5032U          /* VBW: Vertical Boundary Width */
#define OS04C10_DRAW_WINDOW_VBW_CTRL_LOW            0x5033U
#define OS04C10_DRAW_WINDOW_Y_CTRL                  0x5034U
#define OS04C10_DRAW_WINDOW_U_CTRL                  0x5035U
#define OS04C10_DRAW_WINDOW_V_CTRL                  0x5036U
#define OS04C10_PRE_ISP_TEST_SETTING1               0x503DU
#define OS04C10_ISP_SENSOR_BIAS_I                   0x5061U
#define OS04C10_ISP_SENSOR_GAIN1_I                  0x5062U
#define OS04C10_ISP_SENSOR_GAIN2_I                  0x5063U

/* AWB control [0x5180 ~ 0x51D0]                         */
#define OS04C10_AWB_CTRL00                          0x5180U
#define OS04C10_AWB_CTRL01                          0x5181U
#define OS04C10_AWB_CTRL02                          0x5182U
#define OS04C10_AWB_CTRL03                          0x5183U
#define OS04C10_AWB_CTRL04                          0x5184U
#define OS04C10_AWB_CTRL05                          0x5185U
#define OS04C10_AWB_CTRL06                          0x5186U     /* Advanced AWB control registers: 0x5186 ~ 0x5190 */
#define OS04C10_AWB_CTRL07                          0x5187U
#define OS04C10_AWB_CTRL08                          0x5188U
#define OS04C10_AWB_CTRL09                          0x5189U
#define OS04C10_AWB_CTRL10                          0x518AU
#define OS04C10_AWB_CTRL11                          0x518BU
#define OS04C10_AWB_CTRL12                          0x518CU
#define OS04C10_AWB_CTRL13                          0x518DU
#define OS04C10_AWB_CTRL14                          0x518EU
#define OS04C10_AWB_CTRL15                          0x518FU
#define OS04C10_AWB_CTRL16                          0x5190U
#define OS04C10_AWB_CTRL17                          0x5191U
#define OS04C10_AWB_CTRL18                          0x5192U
#define OS04C10_AWB_CTRL19                          0x5193U
#define OS04C10_AWB_CTRL20                          0x5194U
#define OS04C10_AWB_CTRL21                          0x5195U
#define OS04C10_AWB_CTRL22                          0x5196U
#define OS04C10_AWB_CTRL23                          0x5197U
#define OS04C10_AWB_CTRL24                          0x5198U
#define OS04C10_AWB_CTRL25                          0x5199U
#define OS04C10_AWB_CTRL26                          0x519AU
#define OS04C10_AWB_CTRL27                          0x519BU
#define OS04C10_AWB_CTRL28                          0x519CU
#define OS04C10_AWB_CTRL29                          0x519DU
#define OS04C10_AWB_CTRL30                          0x519EU
#define OS04C10_AWB_CURRENT_R_GAIN_HIGH             0x519FU
#define OS04C10_AWB_CURRENT_R_GAIN_LOW              0x51A0U
#define OS04C10_AWB_CURRENT_G_GAIN_HIGH             0x51A1U
#define OS04C10_AWB_CURRENT_G_GAIN_LOW              0x51A2U
#define OS04C10_AWB_CURRENT_B_GAIN_HIGH             0x51A3U
#define OS04C10_AWB_CURRENT_B_GAIN_LOW              0x51A4U
#define OS04C10_AWB_AVERAGE_R                       0x51A5U
#define OS04C10_AWB_AVERAGE_G                       0x51A6U
#define OS04C10_AWB_AVERAGE_B                       0x51A7U
#define OS04C10_AWB_CTRL74                          0x5180U

/* CIP control [0x5300 ~ 0x530F]                         */
#define OS04C10_CIP_SHARPENMT_TH1                   0x5300U
#define OS04C10_CIP_SHARPENMT_TH2                   0x5301U
#define OS04C10_CIP_SHARPENMT_OFFSET1               0x5302U
#define OS04C10_CIP_SHARPENMT_OFFSET2               0x5303U
#define OS04C10_CIP_DNS_TH1                         0x5304U
#define OS04C10_CIP_DNS_TH2                         0x5305U
#define OS04C10_CIP_DNS_OFFSET1                     0x5306U
#define OS04C10_CIP_DNS_OFFSET2                     0x5307U
#define OS04C10_CIP_CTRL                            0x5308U
#define OS04C10_CIP_SHARPENTH_TH1                   0x5309U
#define OS04C10_CIP_SHARPENTH_TH2                   0x530AU
#define OS04C10_CIP_SHARPENTH_OFFSET1               0x530BU
#define OS04C10_CIP_SHARPENTH_OFFSET2               0x530CU
#define OS04C10_CIP_EDGE_MT_AUTO                    0x530DU
#define OS04C10_CIP_DNS_TH_AUTO                     0x530EU
#define OS04C10_CIP_SHARPEN_TH_AUTO                 0x530FU

/* CMX control [0x5380 ~ 0x538B]                         */
#define OS04C10_CMX_CTRL                            0x5380U
#define OS04C10_CMX1                                0x5381U
#define OS04C10_CMX2                                0x5382U
#define OS04C10_CMX3                                0x5383U
#define OS04C10_CMX4                                0x5384U
#define OS04C10_CMX5                                0x5385U
#define OS04C10_CMX6                                0x5386U
#define OS04C10_CMX7                                0x5387U
#define OS04C10_CMX8                                0x5388U
#define OS04C10_CMX9                                0x5389U
#define OS04C10_CMXSIGN_HIGH                        0x538AU
#define OS04C10_CMXSIGN_LOW                         0x538BU

/* gamma control [0x5480 ~ 0x5490]                       */
#define OS04C10_GAMMA_CTRL00                        0x5480U
#define OS04C10_GAMMA_YST00                         0x5481U
#define OS04C10_GAMMA_YST01                         0x5482U
#define OS04C10_GAMMA_YST02                         0x5483U
#define OS04C10_GAMMA_YST03                         0x5484U
#define OS04C10_GAMMA_YST04                         0x5485U
#define OS04C10_GAMMA_YST05                         0x5486U
#define OS04C10_GAMMA_YST06                         0x5487U
#define OS04C10_GAMMA_YST07                         0x5488U
#define OS04C10_GAMMA_YST08                         0x5489U
#define OS04C10_GAMMA_YST09                         0x548AU
#define OS04C10_GAMMA_YST0A                         0x548BU
#define OS04C10_GAMMA_YST0B                         0x548CU
#define OS04C10_GAMMA_YST0C                         0x548DU
#define OS04C10_GAMMA_YST0D                         0x548EU
#define OS04C10_GAMMA_YST0E                         0x548FU
#define OS04C10_GAMMA_YST0F                         0x5490U

/* SDE control [0x5580 ~ 0x558C]                         */
#define OS04C10_SDE_CTRL0                           0x5580U
#define OS04C10_SDE_CTRL1                           0x5581U
#define OS04C10_SDE_CTRL2                           0x5582U
#define OS04C10_SDE_CTRL3                           0x5583U
#define OS04C10_SDE_CTRL4                           0x5584U
#define OS04C10_SDE_CTRL5                           0x5585U
#define OS04C10_SDE_CTRL6                           0x5586U
#define OS04C10_SDE_CTRL7                           0x5587U
#define OS04C10_SDE_CTRL8                           0x5588U
#define OS04C10_SDE_CTRL9                           0x5589U
#define OS04C10_SDE_CTRL10                          0x558AU
#define OS04C10_SDE_CTRL11                          0x558BU
#define OS04C10_SDE_CTRL12                          0x558CU

/* scale control [0x5600 ~ 0x5606]                       */
#define OS04C10_SCALE_CTRL0                         0x5600U
#define OS04C10_SCALE_CTRL1                         0x5601U
#define OS04C10_SCALE_CTRL2                         0x5602U
#define OS04C10_SCALE_CTRL3                         0x5603U
#define OS04C10_SCALE_CTRL4                         0x5604U
#define OS04C10_SCALE_CTRL5                         0x5605U
#define OS04C10_SCALE_CTRL6                         0x5606U


/* AVG control [0x5680 ~ 0x56A2]                         */
#define OS04C10_X_START_HIGH                        0x5680U
#define OS04C10_X_START_LOW                         0x5681U
#define OS04C10_Y_START_HIGH                        0x5682U
#define OS04C10_Y_START_LOW                         0x5683U
#define OS04C10_X_WINDOW_HIGH                       0x5684U
#define OS04C10_X_WINDOW_LOW                        0x5685U
#define OS04C10_Y_WINDOW_HIGH                       0x5686U
#define OS04C10_Y_WINDOW_LOW                        0x5687U
#define OS04C10_WEIGHT00                            0x5688U
#define OS04C10_WEIGHT01                            0x5689U
#define OS04C10_WEIGHT02                            0x568AU
#define OS04C10_WEIGHT03                            0x568BU
#define OS04C10_WEIGHT04                            0x568CU
#define OS04C10_WEIGHT05                            0x568DU
#define OS04C10_WEIGHT06                            0x568EU
#define OS04C10_WEIGHT07                            0x568FU
#define OS04C10_AVG_CTRL10                          0x5690U
#define OS04C10_AVG_WIN_00                          0x5691U
#define OS04C10_AVG_WIN_01                          0x5692U
#define OS04C10_AVG_WIN_02                          0x5693U
#define OS04C10_AVG_WIN_03                          0x5694U
#define OS04C10_AVG_WIN_10                          0x5695U
#define OS04C10_AVG_WIN_11                          0x5696U
#define OS04C10_AVG_WIN_12                          0x5697U
#define OS04C10_AVG_WIN_13                          0x5698U
#define OS04C10_AVG_WIN_20                          0x5699U
#define OS04C10_AVG_WIN_21                          0x569AU
#define OS04C10_AVG_WIN_22                          0x569BU
#define OS04C10_AVG_WIN_23                          0x569CU
#define OS04C10_AVG_WIN_30                          0x569DU
#define OS04C10_AVG_WIN_31                          0x569EU
#define OS04C10_AVG_WIN_32                          0x569FU
#define OS04C10_AVG_WIN_33                          0x56A0U
#define OS04C10_AVG_READOUT                         0x56A1U
#define OS04C10_AVG_WEIGHT_SUM                      0x56A2U

/* LENC control [0x5800 ~ 0x5849]                        */
#define OS04C10_GMTRX00                             0x5800U
#define OS04C10_GMTRX01                             0x5801U
#define OS04C10_GMTRX02                             0x5802U
#define OS04C10_GMTRX03                             0x5803U
#define OS04C10_GMTRX04                             0x5804U
#define OS04C10_GMTRX05                             0x5805U
#define OS04C10_GMTRX10                             0x5806U
#define OS04C10_GMTRX11                             0x5807U
#define OS04C10_GMTRX12                             0x5808U
#define OS04C10_GMTRX13                             0x5809U
#define OS04C10_GMTRX14                             0x580AU
#define OS04C10_GMTRX15                             0x580BU
#define OS04C10_GMTRX20                             0x580CU
#define OS04C10_GMTRX21                             0x580DU
#define OS04C10_GMTRX22                             0x580EU
#define OS04C10_GMTRX23                             0x580FU
#define OS04C10_GMTRX24                             0x5810U
#define OS04C10_GMTRX25                             0x5811U
#define OS04C10_GMTRX30                             0x5812U
#define OS04C10_GMTRX31                             0x5813U
#define OS04C10_GMTRX32                             0x5814U
#define OS04C10_GMTRX33                             0x5815U
#define OS04C10_GMTRX34                             0x5816U
#define OS04C10_GMTRX35                             0x5817U
#define OS04C10_GMTRX40                             0x5818U
#define OS04C10_GMTRX41                             0x5819U
#define OS04C10_GMTRX42                             0x581AU
#define OS04C10_GMTRX43                             0x581BU
#define OS04C10_GMTRX44                             0x581CU
#define OS04C10_GMTRX45                             0x581DU
#define OS04C10_GMTRX50                             0x581EU
#define OS04C10_GMTRX51                             0x581FU
#define OS04C10_GMTRX52                             0x5820U
#define OS04C10_GMTRX53                             0x5821U
#define OS04C10_GMTRX54                             0x5822U
#define OS04C10_GMTRX55                             0x5823U
#define OS04C10_BRMATRX00                           0x5824U
#define OS04C10_BRMATRX01                           0x5825U
#define OS04C10_BRMATRX02                           0x5826U
#define OS04C10_BRMATRX03                           0x5827U
#define OS04C10_BRMATRX04                           0x5828U
#define OS04C10_BRMATRX05                           0x5829U
#define OS04C10_BRMATRX06                           0x582AU
#define OS04C10_BRMATRX07                           0x582BU
#define OS04C10_BRMATRX08                           0x582CU
#define OS04C10_BRMATRX09                           0x582DU
#define OS04C10_BRMATRX20                           0x582EU
#define OS04C10_BRMATRX21                           0x582FU
#define OS04C10_BRMATRX22                           0x5830U
#define OS04C10_BRMATRX23                           0x5831U
#define OS04C10_BRMATRX24                           0x5832U
#define OS04C10_BRMATRX30                           0x5833U
#define OS04C10_BRMATRX31                           0x5834U
#define OS04C10_BRMATRX32                           0x5835U
#define OS04C10_BRMATRX33                           0x5836U
#define OS04C10_BRMATRX34                           0x5837U
#define OS04C10_BRMATRX40                           0x5838U
#define OS04C10_BRMATRX41                           0x5839U
#define OS04C10_BRMATRX42                           0x583AU
#define OS04C10_BRMATRX43                           0x583BU
#define OS04C10_BRMATRX44                           0x583CU
#define OS04C10_LENC_BR_OFFSET                      0x583DU
#define OS04C10_MAX_GAIN                            0x583EU
#define OS04C10_MIN_GAIN                            0x583FU
#define OS04C10_MIN_Q                               0x5840U
#define OS04C10_LENC_CTRL59                         0x5841U
#define OS04C10_BR_HSCALE_HIGH                      0x5842U
#define OS04C10_BR_HSCALE_LOW                       0x5843U
#define OS04C10_BR_VSCALE_HIGH                      0x5844U
#define OS04C10_BR_VSCALE_LOW                       0x5845U
#define OS04C10_G_HSCALE_HIGH                       0x5846U
#define OS04C10_G_HSCALE_LOW                        0x5847U
#define OS04C10_G_VSCALE_HIGH                       0x5848U
#define OS04C10_G_VSCALE_LOW                        0x5849U

/* AFC control [0x6000 ~ 0x603F]                         */
#define OS04C10_AFC_CTRL00                          0x6000U
#define OS04C10_AFC_CTRL01                          0x6001U
#define OS04C10_AFC_CTRL02                          0x6002U
#define OS04C10_AFC_CTRL03                          0x6003U
#define OS04C10_AFC_CTRL04                          0x6004U
#define OS04C10_AFC_CTRL05                          0x6005U
#define OS04C10_AFC_CTRL06                          0x6006U
#define OS04C10_AFC_CTRL07                          0x6007U
#define OS04C10_AFC_CTRL08                          0x6008U
#define OS04C10_AFC_CTRL09                          0x6009U
#define OS04C10_AFC_CTRL10                          0x600AU
#define OS04C10_AFC_CTRL11                          0x600BU
#define OS04C10_AFC_CTRL12                          0x600CU
#define OS04C10_AFC_CTRL13                          0x600DU
#define OS04C10_AFC_CTRL14                          0x600EU
#define OS04C10_AFC_CTRL15                          0x600FU
#define OS04C10_AFC_CTRL16                          0x6010U
#define OS04C10_AFC_CTRL17                          0x6011U
#define OS04C10_AFC_CTRL18                          0x6012U
#define OS04C10_AFC_CTRL19                          0x6013U
#define OS04C10_AFC_CTRL20                          0x6014U
#define OS04C10_AFC_CTRL21                          0x6015U
#define OS04C10_AFC_CTRL22                          0x6016U
#define OS04C10_AFC_CTRL23                          0x6017U
#define OS04C10_AFC_CTRL24                          0x6018U
#define OS04C10_AFC_CTRL25                          0x6019U
#define OS04C10_AFC_CTRL26                          0x601AU
#define OS04C10_AFC_CTRL27                          0x601BU
#define OS04C10_AFC_CTRL28                          0x601CU
#define OS04C10_AFC_CTRL29                          0x601DU
#define OS04C10_AFC_CTRL30                          0x601EU
#define OS04C10_AFC_CTRL31                          0x601FU
#define OS04C10_AFC_CTRL32                          0x6020U
#define OS04C10_AFC_CTRL33                          0x6021U
#define OS04C10_AFC_CTRL34                          0x6022U
#define OS04C10_AFC_CTRL35                          0x6023U
#define OS04C10_AFC_CTRL36                          0x6024U
#define OS04C10_AFC_CTRL37                          0x6025U
#define OS04C10_AFC_CTRL38                          0x6026U
#define OS04C10_AFC_CTRL39                          0x6027U
#define OS04C10_AFC_CTRL40                          0x6028U
#define OS04C10_AFC_CTRL41                          0x6029U
#define OS04C10_AFC_CTRL42                          0x602AU
#define OS04C10_AFC_CTRL43                          0x602BU
#define OS04C10_AFC_CTRL44                          0x602CU
#define OS04C10_AFC_CTRL45                          0x602DU
#define OS04C10_AFC_CTRL46                          0x602EU
#define OS04C10_AFC_CTRL47                          0x602FU
#define OS04C10_AFC_CTRL48                          0x6030U
#define OS04C10_AFC_CTRL49                          0x6031U
#define OS04C10_AFC_CTRL50                          0x6032U
#define OS04C10_AFC_CTRL51                          0x6033U
#define OS04C10_AFC_CTRL52                          0x6034U
#define OS04C10_AFC_CTRL53                          0x6035U
#define OS04C10_AFC_CTRL54                          0x6036U
#define OS04C10_AFC_CTRL55                          0x6037U
#define OS04C10_AFC_CTRL56                          0x6038U
#define OS04C10_AFC_CTRL57                          0x6039U
#define OS04C10_AFC_CTRL58                          0x603AU
#define OS04C10_AFC_CTRL59                          0x603BU
#define OS04C10_AFC_CTRL60                          0x603CU
#define OS04C10_AFC_READ58                          0x603DU
#define OS04C10_AFC_READ59                          0x603EU
#define OS04C10_AFC_READ60                          0x603FU

/**
  * @}
  */

/************** Generic Function  *******************/

typedef int32_t (*OS04C10_Write_Func)(void *, uint16_t, uint8_t *, uint16_t);
typedef int32_t (*OS04C10_Read_Func)(void *, uint16_t, uint8_t *, uint16_t);

typedef struct
{
  OS04C10_Write_Func   WriteReg;
  OS04C10_Read_Func    ReadReg;
  void                *handle;
} os04c10_ctx_t;

/*******************************************************************************
  * Register      : Generic - All
  * Address       : Generic - All
  * Bit Group Name: None
  * Permission    : W
  *******************************************************************************/
int32_t os04c10_write_reg(os04c10_ctx_t *ctx, uint16_t reg, uint8_t *pdata, uint16_t length);
int32_t os04c10_read_reg(os04c10_ctx_t *ctx, uint16_t reg, uint8_t *pdata, uint16_t length);

int32_t os04c10_register_set(os04c10_ctx_t *ctx, uint16_t reg, uint8_t value);
int32_t os04c10_register_get(os04c10_ctx_t *ctx, uint16_t reg, uint8_t *value);


/**
  * @}
  */
#ifdef __cplusplus
}
#endif

#endif /* OS04C10_REG_H */
/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
