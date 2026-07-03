#include "tunnel.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "relay_protocol.h"
#include "status_led.h"
#include "tcp_utils.h"

static const char *TAG = "Tunnel";

enum {
    MAX_TUNNELS = 4,
};

typedef struct {
    uint32_t id;
    int fd;
    bool active;
    int64_t last_activity_ms;
} tunnel_connection_t;

static tunnel_connection_t s_tunnels[MAX_TUNNELS];
static uint8_t s_pipe_buffer[MAX_FRAME_PAYLOAD];
static uint64_t s_bytes_from_server;
static uint64_t s_bytes_from_terminal;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static tunnel_connection_t *find_tunnel(uint32_t id)
{
    for (int i = 0; i < MAX_TUNNELS; i++) {
        if (s_tunnels[i].active && s_tunnels[i].id == id) {
            return &s_tunnels[i];
        }
    }
    return NULL;
}

static tunnel_connection_t *allocate_tunnel(uint32_t id)
{
    tunnel_connection_t *existing = find_tunnel(id);
    if (existing) {
        return existing;
    }

    for (int i = 0; i < MAX_TUNNELS; i++) {
        if (!s_tunnels[i].active) {
            s_tunnels[i].id = id;
            s_tunnels[i].fd = -1;
            s_tunnels[i].active = true;
            s_tunnels[i].last_activity_ms = now_ms();
            return &s_tunnels[i];
        }
    }
    return NULL;
}

void tunnel_init(void)
{
    for (int i = 0; i < MAX_TUNNELS; i++) {
        s_tunnels[i].fd = -1;
    }
}

void tunnel_close(uint32_t id)
{
    tunnel_connection_t *tunnel = find_tunnel(id);
    if (!tunnel) {
        return;
    }

    ESP_LOGI(TAG, "close tunnel id=%" PRIu32, id);
    tcp_close_fd(&tunnel->fd);
    tunnel->active = false;
    tunnel->id = 0;
}

void tunnel_close_all(void)
{
    for (int i = 0; i < MAX_TUNNELS; i++) {
        if (s_tunnels[i].active) {
            tunnel_close(s_tunnels[i].id);
        }
    }
}

int tunnel_active_count(void)
{
    int active_tunnels = 0;
    for (int i = 0; i < MAX_TUNNELS; i++) {
        if (s_tunnels[i].active) {
            active_tunnels++;
        }
    }
    return active_tunnels;
}

uint64_t tunnel_bytes_from_server(void)
{
    return s_bytes_from_server;
}

uint64_t tunnel_bytes_from_terminal(void)
{
    return s_bytes_from_terminal;
}

void tunnel_handle_open(int relay_fd, uint32_t connection_id, const uint8_t *payload, uint32_t length, const remote_config_t *config)
{
    char json[256];
    size_t copy_len = length < sizeof(json) - 1 ? length : sizeof(json) - 1;
    memcpy(json, payload, copy_len);
    json[copy_len] = '\0';

    char host[64];
    strlcpy(host, config->terminal_rdp_host, sizeof(host));
    uint16_t port = config->terminal_rdp_port;
    relay_extract_json_string(json, "host", host, sizeof(host));
    relay_extract_json_u16(json, "port", &port);

    tunnel_connection_t *tunnel = allocate_tunnel(connection_id);
    if (!tunnel) {
        relay_send_text_frame(relay_fd, FRAME_ERROR, connection_id, "no tunnel slot");
        relay_send_frame(relay_fd, FRAME_CLOSE, connection_id, NULL, 0);
        return;
    }

    tcp_close_fd(&tunnel->fd);
    ESP_LOGI(TAG, "connect terminal conn=%" PRIu32 " target=%s:%u", connection_id, host, port);
    tunnel->fd = tcp_connect_host(host, port, 3000, MAX_FRAME_PAYLOAD * 2);
    if (tunnel->fd < 0) {
        ESP_LOGE(TAG, "terminal connection failed conn=%" PRIu32 " target=%s:%u", connection_id, host, port);
        relay_send_text_frame(relay_fd, FRAME_ERROR, connection_id, "terminal connection failed");
        relay_send_frame(relay_fd, FRAME_CLOSE, connection_id, NULL, 0);
        tunnel_close(connection_id);
        return;
    }

    tunnel->last_activity_ms = now_ms();
    ESP_LOGI(TAG, "terminal connected conn=%" PRIu32, connection_id);
}

void tunnel_handle_data(int relay_fd, uint32_t connection_id, const uint8_t *payload, uint32_t length)
{
    tunnel_connection_t *tunnel = find_tunnel(connection_id);
    if (!tunnel || tunnel->fd < 0) {
        relay_send_frame(relay_fd, FRAME_CLOSE, connection_id, NULL, 0);
        tunnel_close(connection_id);
        return;
    }

    if (tcp_write_all(tunnel->fd, payload, length) != ESP_OK) {
        relay_send_text_frame(relay_fd, FRAME_ERROR, connection_id, "terminal write failed");
        relay_send_frame(relay_fd, FRAME_CLOSE, connection_id, NULL, 0);
        tunnel_close(connection_id);
        return;
    }
    s_bytes_from_server += length;
    status_led_note_data();
    tunnel->last_activity_ms = now_ms();
}

esp_err_t tunnel_pump_terminal_traffic(int relay_fd)
{
    fd_set readfds;
    FD_ZERO(&readfds);
    int max_fd = -1;

    for (int i = 0; i < MAX_TUNNELS; i++) {
        if (s_tunnels[i].active && s_tunnels[i].fd >= 0) {
            FD_SET(s_tunnels[i].fd, &readfds);
            if (s_tunnels[i].fd > max_fd) {
                max_fd = s_tunnels[i].fd;
            }
        }
    }

    if (max_fd < 0) {
        return ESP_OK;
    }

    struct timeval tv = {.tv_sec = 0, .tv_usec = 0};
    int selected = select(max_fd + 1, &readfds, NULL, NULL, &tv);
    if (selected <= 0) {
        return ESP_OK;
    }

    for (int i = 0; i < MAX_TUNNELS; i++) {
        tunnel_connection_t *tunnel = &s_tunnels[i];
        if (!tunnel->active || tunnel->fd < 0 || !FD_ISSET(tunnel->fd, &readfds)) {
            continue;
        }

        int received = recv(tunnel->fd, s_pipe_buffer, sizeof(s_pipe_buffer), 0);
        if (received <= 0) {
            relay_send_frame(relay_fd, FRAME_CLOSE, tunnel->id, NULL, 0);
            tunnel_close(tunnel->id);
            continue;
        }

        if (relay_send_frame(relay_fd, FRAME_DATA, tunnel->id, s_pipe_buffer, received) != ESP_OK) {
            return ESP_FAIL;
        }
        s_bytes_from_terminal += received;
        status_led_note_data();
        tunnel->last_activity_ms = now_ms();
    }

    return ESP_OK;
}
