#include "driver_core.h"
#include "debug.h"
#include "storage.h"
#include "drtc.h"
#include "camera.h"
#include "enc.h"
#include "draw.h"
// #include "ai_process.h"
#include "wifi.h"
#include "misc.h"
#include "pwr.h"
#include "codec.h"
#include "cat1.h"
#include "sd_file.h"
// #include "usb_host_video.h"
#include "netif_manager.h"
#include "wdg.h"
#include "jpegc.h"
#include "tls_test.h"
#include "driver_test.h"
#include "nn.h"
#include "mem.h"
#include "u0_module.h"
#include "system_top.h"
#include "upgrade_manager.h"
#include "SensorExt/i2c_tool/i2c_tool.h"
#include "SensorExt/tft_st7789v/tft_st7789vw.h"
#include "SensorExt/sensor_exemple/sensor_exemple.h"

bool driver_core_init(void)
{
    printf("driver_core_init \r\n");
    hal_mem_register();
    storage_register();
#if ENABLE_U0_MODULE
    u0_module_register();
#endif
    wdg_register();
    pwr_register();
    sd_register();
    misc_register();
    rtc_register();
#if !defined(POWER_MODULE_TEST) || !POWER_MODULE_TEST
    camera_register();
    // usbvideo_register();
    draw_register();
    // ai_register();
    enc_register();
    jpegc_register();
    // codec_register();
    nn_register();
#else
    netif_manager_register();
#endif
    netif_manager_register_commands();
    // wifi_register();
    // tls_test_register();
    // cat1_register();
    system_top_register();
    i2c_tool_register();
    tft_st7789vw_register_commands();
    sensor_exemple_register_commands();

    LOG_DRV_DEBUG("driver_core_init end \r\n");
    driver_test_main();
    set_slot_boot_success(FIRMWARE_APP, true);
    return true;
}

