#include "usb_host_ecm.h"
#include "cat1.h"
#include "ux_hcd_stm32.h"
#include "ux_system.h"
#include "ux_utility.h"
#include "pwr.h"
#include <stdint.h>  // For uintptr_t

static UX_HOST_CLASS_CDC_ECM *ux_host_cdc_ecm = NULL;
static ux_host_config_t g_ux_host_config = {0};
static usb_host_ecm_event_callback_t g_usb_host_ecm_event_callback = NULL;

void usb_host_ecm_error_callback(UINT system_level, UINT system_context, UINT error_code)
{
    printf("USB ECM Error: 0x%X\r\n ", (unsigned int)error_code);
    if (g_usb_host_ecm_event_callback != NULL) {
        g_usb_host_ecm_event_callback(USB_HOST_ECM_EVENT_ERROR, (void *)(uintptr_t)error_code);
    }
    switch (error_code) {
        case UX_DEVICE_ENUMERATION_FAILURE:
            printf("USB ECM Enumeration Failure\r\n");
            break;
        case UX_NO_DEVICE_CONNECTED:
            printf("USB ECM Disconnected\r\n");
            break;
        default:
            break;
    }
}

static void usb_host_ecm_deactivate_callback(UX_HOST_CLASS_CDC_ECM *instance)
{
    if (g_usb_host_ecm_event_callback != NULL) {
        g_usb_host_ecm_event_callback(USB_HOST_ECM_EVENT_DEACTIVATE, NULL);
    }
}

static void usb_host_ecm_up_callback(UX_HOST_CLASS_CDC_ECM *instance)
{
    if (g_usb_host_ecm_event_callback != NULL) {
        g_usb_host_ecm_event_callback(USB_HOST_ECM_EVENT_UP, NULL);
    }
}

static void usb_host_ecm_down_callback(UX_HOST_CLASS_CDC_ECM *instance)
{
    if (g_usb_host_ecm_event_callback != NULL) {
        g_usb_host_ecm_event_callback(USB_HOST_ECM_EVENT_DOWN, NULL);
    }
}

static void usb_host_ecm_receive_callback(UX_HOST_CLASS_CDC_ECM *instance, NX_PACKET *packet)
{
    if (g_usb_host_ecm_event_callback != NULL) {
        g_usb_host_ecm_event_callback(USB_HOST_ECM_EVENT_DATA, packet);
    }
}

UINT ux_host_ecm_event_callback(ULONG event, UX_HOST_CLASS *current_class, void *current_instance)
{
    UINT status = UX_SUCCESS;
    printf("USB ECM Event:0x%lX\r\n", event);
    switch (event) {
        case UX_DEVICE_INSERTION:
            if (current_class->ux_host_class_entry_function == ux_host_class_cdc_ecm_entry) {
                ux_host_cdc_ecm = (UX_HOST_CLASS_CDC_ECM *)current_instance;
                if (g_usb_host_ecm_event_callback != NULL) {
                    g_usb_host_ecm_event_callback(USB_HOST_ECM_EVENT_ACTIVATE, ux_host_cdc_ecm->ux_host_class_cdc_ecm_node_id);
                }
                ux_host_cdc_ecm->ux_host_class_cdc_ecm_deactivate_callback = usb_host_ecm_deactivate_callback;
                ux_host_cdc_ecm->ux_host_class_cdc_ecm_link_up_callback = usb_host_ecm_up_callback;
                ux_host_cdc_ecm->ux_host_class_cdc_ecm_link_down_callback = usb_host_ecm_down_callback;
                ux_host_cdc_ecm->ux_host_class_cdc_ecm_receive_callback = usb_host_ecm_receive_callback;
                printf("USB ECM Inserted\r\n");
            }
            break;
        case UX_DEVICE_REMOVAL:
            if ((VOID *)ux_host_cdc_ecm == current_instance) {
                printf("USB ECM Removed\r\n");
                ux_host_cdc_ecm = NULL;
            }
            break;
        case UX_DEVICE_CONNECTION:
            if (ux_host_cdc_ecm != NULL) {
                printf("USB ECM Connected\r\n");
                printf("PID: %#x\r\n",(UINT)_ux_system_host->ux_system_host_device_array->ux_device_descriptor.idProduct);
                printf("VID: %#x\r\n", (UINT)_ux_system_host->ux_system_host_device_array->ux_device_descriptor.idVendor);
            }
            break;
        default:
            break;
    }
    return status;
}

int usb_host_ecm_init(usb_host_ecm_event_callback_t event_callback)
{
    int ret = 0;
    if (ux_host_cdc_ecm != NULL) return -1;

    g_usb_host_ecm_event_callback = event_callback;
    pwr_manager_acquire(pwr_manager_get_handle(PWR_USB_NAME));
    osDelay(100);
    
    g_ux_host_config.error_callback = usb_host_ecm_error_callback;
    g_ux_host_config.event_callback = ux_host_ecm_event_callback;
    g_ux_host_config.class_name = _ux_system_host_class_cdc_ecm_name;
    g_ux_host_config.class_entry_function = ux_host_class_cdc_ecm_entry;
    g_ux_host_config.hcd_name = _ux_system_host_hcd_stm32_name;
    g_ux_host_config.hcd_init_function = _ux_hcd_stm32_initialize;
    g_ux_host_config.is_uninit_memory = false;
    ret = USBX_Host_Init(&g_ux_host_config);
    if (ret != 0) {
        printf("USB ECM Initialization Failed: 0x%X", ret);
        return ret;
    }
    return ret;
}

int usb_host_ecm_send_raw_data(NX_PACKET *packet)
{
    if (ux_host_cdc_ecm == NULL) return -1;
    return ux_host_class_cdc_ecm_write(ux_host_cdc_ecm, packet);
}

void usb_host_ecm_deinit(void)
{
    g_usb_host_ecm_event_callback = NULL;
    pwr_manager_release(pwr_manager_get_handle(PWR_USB_NAME));
    osDelay(100);
    USBX_Host_Deinit(&g_ux_host_config);
    ux_host_cdc_ecm = NULL;
}