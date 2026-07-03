#include "remote_config.h"

#include <stddef.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "RemoteConfig";

static const char *DEFAULT_WIFI_SSID = CONFIG_REMOTE_AGENT_WIFI_SSID;
static const char *DEFAULT_WIFI_PASSWORD = CONFIG_REMOTE_AGENT_WIFI_PASSWORD;
static const char *DEFAULT_SERVER_HOST = CONFIG_REMOTE_AGENT_SERVER_HOST;
static const uint16_t DEFAULT_SERVER_CONTROL_PORT = CONFIG_REMOTE_AGENT_SERVER_CONTROL_PORT;
static const char *DEFAULT_BOARD_ID = CONFIG_REMOTE_AGENT_BOARD_ID;
static const char *DEFAULT_BOARD_KEY = CONFIG_REMOTE_AGENT_BOARD_KEY;
static const uint16_t DEFAULT_ASSIGNED_PUBLIC_PORT = CONFIG_REMOTE_AGENT_ASSIGNED_PUBLIC_PORT;
static const char *DEFAULT_TERMINAL_RDP_HOST = CONFIG_REMOTE_AGENT_TERMINAL_RDP_HOST;
static const uint16_t DEFAULT_TERMINAL_RDP_PORT = CONFIG_REMOTE_AGENT_TERMINAL_RDP_PORT;

static void config_set_defaults(remote_config_t *cfg)
{
    strlcpy(cfg->wifi_ssid, DEFAULT_WIFI_SSID, sizeof(cfg->wifi_ssid));
    strlcpy(cfg->wifi_password, DEFAULT_WIFI_PASSWORD, sizeof(cfg->wifi_password));
    strlcpy(cfg->server_host, DEFAULT_SERVER_HOST, sizeof(cfg->server_host));
    cfg->server_control_port = DEFAULT_SERVER_CONTROL_PORT;
    strlcpy(cfg->board_id, DEFAULT_BOARD_ID, sizeof(cfg->board_id));
    strlcpy(cfg->board_key, DEFAULT_BOARD_KEY, sizeof(cfg->board_key));
    cfg->assigned_public_port = DEFAULT_ASSIGNED_PUBLIC_PORT;
    strlcpy(cfg->terminal_rdp_host, DEFAULT_TERMINAL_RDP_HOST, sizeof(cfg->terminal_rdp_host));
    cfg->terminal_rdp_port = DEFAULT_TERMINAL_RDP_PORT;
}

static void nvs_get_string_or_keep(nvs_handle_t nvs, const char *key, char *value, size_t value_size)
{
    size_t required = value_size;
    esp_err_t err = nvs_get_str(nvs, key, value, &required);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "read config %s failed: %s", key, esp_err_to_name(err));
    }
}

static void nvs_get_u16_or_keep(nvs_handle_t nvs, const char *key, uint16_t *value)
{
    uint16_t stored = 0;
    esp_err_t err = nvs_get_u16(nvs, key, &stored);
    if (err == ESP_OK) {
        *value = stored;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "read config %s failed: %s", key, esp_err_to_name(err));
    }
}

static esp_err_t config_save_defaults_if_missing(nvs_handle_t nvs, const remote_config_t *cfg)
{
    size_t required = 0;
    if (nvs_get_str(nvs, "wifi_ssid", NULL, &required) == ESP_ERR_NVS_NOT_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_set_str(nvs, "wifi_ssid", cfg->wifi_ssid), TAG, "save wifi ssid failed");
        ESP_RETURN_ON_ERROR(nvs_set_str(nvs, "wifi_pass", cfg->wifi_password), TAG, "save wifi password failed");
        ESP_RETURN_ON_ERROR(nvs_set_str(nvs, "server", cfg->server_host), TAG, "save server failed");
        ESP_RETURN_ON_ERROR(nvs_set_u16(nvs, "ctrl_port", cfg->server_control_port), TAG, "save control port failed");
        ESP_RETURN_ON_ERROR(nvs_set_str(nvs, "board_id", cfg->board_id), TAG, "save board id failed");
        ESP_RETURN_ON_ERROR(nvs_set_str(nvs, "board_key", cfg->board_key), TAG, "save board key failed");
        ESP_RETURN_ON_ERROR(nvs_set_u16(nvs, "public_port", cfg->assigned_public_port), TAG, "save public port failed");
        ESP_RETURN_ON_ERROR(nvs_set_str(nvs, "target_host", cfg->terminal_rdp_host), TAG, "save target host failed");
        ESP_RETURN_ON_ERROR(nvs_set_u16(nvs, "target_port", cfg->terminal_rdp_port), TAG, "save target port failed");
        ESP_RETURN_ON_ERROR(nvs_commit(nvs), TAG, "commit config failed");
        ESP_LOGI(TAG, "factory defaults saved to NVS namespace remote_cfg");
    }

    return ESP_OK;
}

esp_err_t remote_config_load(remote_config_t *cfg)
{
    config_set_defaults(cfg);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("remote_cfg", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open remote_cfg failed, using compile defaults: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(config_save_defaults_if_missing(nvs, cfg), TAG, "save default config failed");
    nvs_get_string_or_keep(nvs, "wifi_ssid", cfg->wifi_ssid, sizeof(cfg->wifi_ssid));
    nvs_get_string_or_keep(nvs, "wifi_pass", cfg->wifi_password, sizeof(cfg->wifi_password));
    nvs_get_string_or_keep(nvs, "server", cfg->server_host, sizeof(cfg->server_host));
    nvs_get_u16_or_keep(nvs, "ctrl_port", &cfg->server_control_port);
    nvs_get_string_or_keep(nvs, "board_id", cfg->board_id, sizeof(cfg->board_id));
    nvs_get_string_or_keep(nvs, "board_key", cfg->board_key, sizeof(cfg->board_key));
    nvs_get_u16_or_keep(nvs, "public_port", &cfg->assigned_public_port);
    nvs_get_string_or_keep(nvs, "target_host", cfg->terminal_rdp_host, sizeof(cfg->terminal_rdp_host));
    nvs_get_u16_or_keep(nvs, "target_port", &cfg->terminal_rdp_port);
    nvs_close(nvs);
    return ESP_OK;
}
