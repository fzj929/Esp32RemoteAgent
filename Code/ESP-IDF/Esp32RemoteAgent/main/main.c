#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "relay_client.h"
#include "remote_config.h"
#include "status_led.h"
#include "tunnel.h"
#include "usb_net.h"
#include "wifi_client.h"

static const char *TAG = "RemoteAgent";

static remote_config_t s_config;

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 remote RDP agent starting");

    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(remote_config_load(&s_config));
    ESP_LOGI(TAG, "boardId=%s assignedPort=server-assigned server=%s:%u",
             s_config.board_id, s_config.server_host, s_config.server_control_port);

    ESP_ERROR_CHECK(status_led_init());
    tunnel_init();

    ESP_ERROR_CHECK(wifi_client_start(&s_config));
    ESP_ERROR_CHECK(usb_net_start());
    ESP_ERROR_CHECK(relay_client_start(&s_config));
}
