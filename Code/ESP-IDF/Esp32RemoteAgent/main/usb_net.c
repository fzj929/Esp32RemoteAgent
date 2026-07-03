#include "usb_net.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "lwip/esp_netif_net_stack.h"
#include "lwip/ip4_addr.h"
#include "tinyusb.h"
#include "tinyusb_net.h"
#include "tusb.h"

static const char *TAG = "USB_NCM";

static esp_netif_t *s_usb_netif;

#if CONFIG_TINYUSB_NET_MODE_NCM
#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)
#define MS_OS_20_DESC_LEN 0xB2
#define NCM_INTERFACE_NUMBER 0

static const tusb_desc_device_t s_usb_device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4010,
    .bcdDevice = 0x0101,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const uint8_t s_bos_desc[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, 1),
};

static const uint8_t s_ms_os_20_desc[] = {
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), NCM_INTERFACE_NUMBER, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'N', 'C', 'M', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A),
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00,
    'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, 0x00, 0x00,
    U16_TO_U8S_LE(0x0050),
    '{', 0x00, '1', 0x00, '2', 0x00, '3', 0x00, '4', 0x00, '5', 0x00, '6', 0x00, '7', 0x00, '8', 0x00, '-', 0x00,
    '0', 0x00, 'D', 0x00, '0', 0x00, '8', 0x00, '-', 0x00, '4', 0x00, '3', 0x00, 'F', 0x00, 'D', 0x00, '-', 0x00,
    '8', 0x00, 'B', 0x00, '3', 0x00, 'E', 0x00, '-', 0x00, '1', 0x00, '2', 0x00, '7', 0x00, 'C', 0x00, 'A', 0x00,
    '8', 0x00, 'A', 0x00, 'F', 0x00, 'F', 0x00, 'F', 0x00, '9', 0x00, 'D', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00,
};

TU_VERIFY_STATIC(sizeof(s_ms_os_20_desc) == MS_OS_20_DESC_LEN, "Incorrect MS OS 2.0 descriptor size");

uint8_t const *tud_descriptor_bos_cb(void)
{
    return s_bos_desc;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR && request->bRequest == 1 && request->wIndex == 7) {
        uint16_t total_len = 0;
        memcpy(&total_len, s_ms_os_20_desc + 8, sizeof(total_len));
        return tud_control_xfer(rhport, request, (void *)(uintptr_t)s_ms_os_20_desc, total_len);
    }

    return false;
}
#endif

static esp_err_t usb_netif_transmit(void *h, void *buffer, size_t len)
{
    esp_err_t err = tinyusb_net_send_sync(buffer, len, NULL, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "USB send failed: %s", esp_err_to_name(err));
    }
    return err;
}

static void usb_netif_free_rx_buffer(void *h, void *buffer)
{
    free(buffer);
}

static esp_err_t usb_recv_callback(void *buffer, uint16_t len, void *ctx)
{
    if (!s_usb_netif) {
        return ESP_OK;
    }

    void *copy = malloc(len);
    if (!copy) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(copy, buffer, len);
    return esp_netif_receive(s_usb_netif, copy, len, NULL);
}

esp_err_t usb_net_start(void)
{
    ESP_LOGI(TAG, "USB NCM device initialization");

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &s_usb_device_desc,
        .external_phy = false,
    };
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG, "Failed to install TinyUSB driver");

    const tinyusb_net_config_t net_config = {
        .mac_addr = {0x02, 0x02, 0x11, 0x22, 0x33, 0x01},
        .on_recv_callback = usb_recv_callback,
    };
    ESP_RETURN_ON_ERROR(tinyusb_net_init(TINYUSB_USBDEV_0, &net_config), TAG, "Failed to initialize USB NCM device");

    static esp_netif_ip_info_t usb_ip_info;
    IP4_ADDR(&usb_ip_info.ip, 192, 168, 77, 1);
    IP4_ADDR(&usb_ip_info.gw, 192, 168, 77, 1);
    IP4_ADDR(&usb_ip_info.netmask, 255, 255, 255, 0);

    esp_netif_inherent_config_t base_cfg = {
        .flags = ESP_NETIF_DHCP_SERVER | ESP_NETIF_FLAG_AUTOUP,
        .ip_info = &usb_ip_info,
        .if_key = "usb_ncm",
        .if_desc = "usb ncm terminal link",
        .route_prio = 1,
    };

    esp_netif_driver_ifconfig_t driver_cfg = {
        .handle = (void *)1,
        .transmit = usb_netif_transmit,
        .driver_free_rx_buffer = usb_netif_free_rx_buffer,
    };

    struct esp_netif_netstack_config lwip_netif_config = {
        .lwip = {
            .init_fn = ethernetif_init,
            .input_fn = ethernetif_input,
        },
    };

    esp_netif_config_t cfg = {
        .base = &base_cfg,
        .driver = &driver_cfg,
        .stack = &lwip_netif_config,
    };

    s_usb_netif = esp_netif_new(&cfg);
    ESP_RETURN_ON_FALSE(s_usb_netif != NULL, ESP_FAIL, TAG, "Failed to create USB NCM netif");

    uint8_t lwip_mac[6] = {0x02, 0x02, 0x11, 0x22, 0x33, 0x02};
    ESP_RETURN_ON_ERROR(esp_netif_set_mac(s_usb_netif, lwip_mac), TAG, "Failed to set USB netif MAC");

    esp_netif_action_start(s_usb_netif, 0, 0, 0);
    ESP_LOGI(TAG, "USB NCM ready: ESP32 IP 192.168.77.1/24, terminal can use DHCP or 192.168.77.2/24");
    return ESP_OK;
}
