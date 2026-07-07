#include "relay_client.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "relay_protocol.h"
#include "status_led.h"
#include "tcp_utils.h"
#include "tunnel.h"
#include "wifi_client.h"

static const char *TAG = "RelayClient";
static const char *FIRMWARE_VERSION = "esp-idf-s3-0.2.0";

enum {
    HEARTBEAT_INTERVAL_MS = 15000,
    RECONNECT_DELAY_MS = 3000,
};

static remote_config_t *s_config;
static uint8_t s_rx_buffer[MAX_FRAME_PAYLOAD];
static int64_t s_last_heartbeat_ms;
static uint16_t s_runtime_assigned_public_port;
static bool s_relay_online;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static esp_err_t register_board(int relay_fd)
{
    const uint16_t requested_assigned_port = 0;
    char nonce[18];
    snprintf(nonce, sizeof(nonce), "%08" PRIx32 "%08" PRIx32, esp_random(), esp_random());
    int64_t timestamp_ms = now_ms();

    char auth_payload[256];
    snprintf(auth_payload, sizeof(auth_payload), "%s|%u|%s|%u|%s|%s|%" PRIi64,
             s_config->board_id,
             requested_assigned_port,
             s_config->terminal_rdp_host,
             s_config->terminal_rdp_port,
             FIRMWARE_VERSION,
             nonce,
             timestamp_ms);

    char signature[65];
    ESP_RETURN_ON_ERROR(relay_hmac_sha256_hex(s_config->board_key, auth_payload, signature, sizeof(signature)), TAG, "auth signature failed");

    char json[640];
    snprintf(json, sizeof(json),
             "{\"boardId\":\"%s\",\"assignedPort\":%u,"
             "\"targetHost\":\"%s\",\"targetPort\":%u,\"firmware\":\"%s\","
             "\"authNonce\":\"%s\",\"authTimestampMs\":%" PRIi64 ",\"authSignature\":\"%s\"}",
             s_config->board_id,
             requested_assigned_port,
             s_config->terminal_rdp_host,
             s_config->terminal_rdp_port,
             FIRMWARE_VERSION,
             nonce,
             timestamp_ms,
             signature);

    ESP_LOGI(TAG, "register boardId=%s assignedPort=request-server target=%s:%u",
             s_config->board_id, s_config->terminal_rdp_host, s_config->terminal_rdp_port);
    return relay_send_text_frame(relay_fd, FRAME_REGISTER, 0, json);
}

static esp_err_t wait_register_ack(int relay_fd)
{
    uint8_t header[FRAME_HEADER_LEN];
    ESP_RETURN_ON_ERROR(relay_read_exact(relay_fd, header, sizeof(header), 5000), TAG, "registration ack timeout");

    uint8_t type = header[0];
    uint32_t len = relay_read_u32_be(header + 5);
    if (len > MAX_FRAME_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (len > 0) {
        ESP_RETURN_ON_ERROR(relay_read_exact(relay_fd, s_rx_buffer, len, 5000), TAG, "registration ack payload timeout");
    }

    if (type != FRAME_REGISTER_ACK) {
        ESP_LOGE(TAG, "registration rejected frameType=%u len=%" PRIu32, type, len);
        return ESP_FAIL;
    }

    if (len > 0) {
        char ack_json[256];
        size_t ack_len = len < sizeof(ack_json) - 1 ? len : sizeof(ack_json) - 1;
        memcpy(ack_json, s_rx_buffer, ack_len);
        ack_json[ack_len] = '\0';

        uint16_t assigned_port = 0;
        if (relay_extract_json_u16(ack_json, "assignedPort", &assigned_port) && assigned_port > 0) {
            s_runtime_assigned_public_port = assigned_port;
        }

        char target_host[sizeof(s_config->terminal_rdp_host)];
        if (relay_extract_json_string(ack_json, "targetHost", target_host, sizeof(target_host))) {
            strlcpy(s_config->terminal_rdp_host, target_host, sizeof(s_config->terminal_rdp_host));
        }

        uint16_t target_port = 0;
        if (relay_extract_json_u16(ack_json, "targetPort", &target_port) && target_port > 0) {
            s_config->terminal_rdp_port = target_port;
        }
    }

    ESP_LOGI(TAG, "registered successfully assignedPort=%u target=%s:%u",
             s_runtime_assigned_public_port, s_config->terminal_rdp_host, s_config->terminal_rdp_port);
    s_last_heartbeat_ms = now_ms();
    return ESP_OK;
}

static esp_err_t process_relay_frame(int relay_fd)
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(relay_fd, &readfds);

    struct timeval tv = {.tv_sec = 0, .tv_usec = 1000};
    int selected = select(relay_fd + 1, &readfds, NULL, NULL, &tv);
    if (selected < 0) {
        return ESP_FAIL;
    }
    if (selected == 0) {
        return ESP_OK;
    }

    uint8_t header[FRAME_HEADER_LEN];
    esp_err_t err = relay_read_exact(relay_fd, header, sizeof(header), 5000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read relay frame header failed err=0x%x", err);
        return err;
    }

    uint8_t type = header[0];
    uint32_t connection_id = relay_read_u32_be(header + 1);
    uint32_t length = relay_read_u32_be(header + 5);
    if (length > MAX_FRAME_PAYLOAD) {
        ESP_LOGE(TAG, "relay payload too large type=%u conn=%" PRIu32 " len=%" PRIu32, type, connection_id, length);
        return ESP_FAIL;
    }

    if (length > 0) {
        err = relay_read_exact(relay_fd, s_rx_buffer, length, 5000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "read relay payload failed type=%u conn=%" PRIu32 " len=%" PRIu32 " err=0x%x",
                     type, connection_id, length, err);
            return err;
        }
    }

    switch (type) {
    case FRAME_OPEN:
        tunnel_handle_open(relay_fd, connection_id, s_rx_buffer, length, s_config);
        break;
    case FRAME_DATA:
        tunnel_handle_data(relay_fd, connection_id, s_rx_buffer, length);
        break;
    case FRAME_CLOSE:
        tunnel_close(connection_id);
        break;
    default:
        ESP_LOGW(TAG, "unknown relay frame type=%u conn=%" PRIu32 " len=%" PRIu32, type, connection_id, length);
        break;
    }

    return ESP_OK;
}

static void send_heartbeat_if_needed(int relay_fd)
{
    if (now_ms() - s_last_heartbeat_ms < HEARTBEAT_INTERVAL_MS) {
        return;
    }

    wifi_ap_record_t ap_info = {0};
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    char json[320];
    snprintf(json, sizeof(json),
             "{\"uptimeMs\":%" PRIi64 ",\"freeHeap\":%lu,\"rssi\":%d,"
             "\"activeTunnels\":%d,\"bytesFromServer\":%" PRIu64 ","
             "\"bytesFromTerminal\":%" PRIu64 ",\"usbNetif\":\"ncm\","
             "\"firmware\":\"%s\",\"assignedPort\":%u}",
             now_ms(),
             (unsigned long)esp_get_free_heap_size(),
             rssi,
             tunnel_active_count(),
             tunnel_bytes_from_server(),
             tunnel_bytes_from_terminal(),
             FIRMWARE_VERSION,
             s_runtime_assigned_public_port);
    if (relay_send_text_frame(relay_fd, FRAME_HEARTBEAT, 0, json) == ESP_OK) {
        ESP_LOGI(TAG, "heartbeat sent freeHeap=%lu", (unsigned long)esp_get_free_heap_size());
    }
    s_last_heartbeat_ms = now_ms();
}

static void relay_task(void *arg)
{
    while (true) {
        wifi_client_wait_connected();

        ESP_LOGI(TAG, "connecting relay %s:%u", s_config->server_host, s_config->server_control_port);
        int relay_fd = tcp_connect_host(s_config->server_host, s_config->server_control_port, 5000, MAX_FRAME_PAYLOAD * 2);
        if (relay_fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }

        if (register_board(relay_fd) != ESP_OK || wait_register_ack(relay_fd) != ESP_OK) {
            close(relay_fd);
            s_relay_online = false;
            status_led_set_disconnected();
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }

        s_relay_online = true;
        status_led_set_connected();

        while (true) {
            if (process_relay_frame(relay_fd) != ESP_OK) {
                break;
            }
            if (tunnel_pump_terminal_traffic(relay_fd) != ESP_OK) {
                break;
            }
            send_heartbeat_if_needed(relay_fd);
            status_led_tick(s_relay_online);
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        ESP_LOGW(TAG, "relay disconnected, close active tunnels");
        close(relay_fd);
        s_relay_online = false;
        status_led_set_disconnected();
        tunnel_close_all();
        vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
    }
}

esp_err_t relay_client_start(remote_config_t *config)
{
    s_config = config;
    s_runtime_assigned_public_port = 0;

    BaseType_t created = xTaskCreatePinnedToCore(relay_task, "relay_task", 8192, NULL, 5, NULL, 1);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
