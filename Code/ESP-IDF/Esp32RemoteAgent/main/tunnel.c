#include "tunnel.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "relay_protocol.h"
#include "status_led.h"
#include "tcp_utils.h"

static const char *TAG = "Tunnel";

#ifndef CONFIG_REMOTE_AGENT_MAX_TUNNELS
#define CONFIG_REMOTE_AGENT_MAX_TUNNELS 16
#endif

#define MAX_TUNNELS CONFIG_REMOTE_AGENT_MAX_TUNNELS

#ifndef CONFIG_REMOTE_AGENT_PENDING_TX_BUFFER
#define CONFIG_REMOTE_AGENT_PENDING_TX_BUFFER 8192
#endif

#ifndef CONFIG_REMOTE_AGENT_TERMINAL_CONNECT_TIMEOUT_MS
#define CONFIG_REMOTE_AGENT_TERMINAL_CONNECT_TIMEOUT_MS 1500
#endif

typedef enum {
    TUNNEL_CONNECTING,
    TUNNEL_CONNECTED,
} tunnel_state_t;

typedef struct {
    uint32_t id;
    int fd;
    bool active;
    tunnel_state_t state;
    int64_t connect_started_ms;
    int64_t last_activity_ms;
    uint8_t *pending_tx;
    size_t pending_tx_len;
    size_t pending_tx_capacity;
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
            s_tunnels[i].state = TUNNEL_CONNECTING;
            s_tunnels[i].connect_started_ms = now_ms();
            s_tunnels[i].last_activity_ms = now_ms();
            s_tunnels[i].pending_tx = NULL;
            s_tunnels[i].pending_tx_len = 0;
            s_tunnels[i].pending_tx_capacity = 0;
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

static void tunnel_free_pending(tunnel_connection_t *tunnel)
{
    free(tunnel->pending_tx);
    tunnel->pending_tx = NULL;
    tunnel->pending_tx_len = 0;
    tunnel->pending_tx_capacity = 0;
}

static void tunnel_close_reason(uint32_t id, const char *reason)
{
    tunnel_connection_t *tunnel = find_tunnel(id);
    if (!tunnel) {
        return;
    }

    ESP_LOGI(TAG, "close tunnel id=%" PRIu32 " reason=%s", id, reason);
    tcp_close_fd(&tunnel->fd);
    tunnel_free_pending(tunnel);
    tunnel->active = false;
    tunnel->id = 0;
}

void tunnel_close(uint32_t id)
{
    tunnel_close_reason(id, "relay requested close");
}

void tunnel_close_all(void)
{
    for (int i = 0; i < MAX_TUNNELS; i++) {
        if (s_tunnels[i].active) {
            tunnel_close_reason(s_tunnels[i].id, "relay disconnected");
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
        ESP_LOGW(TAG, "no tunnel slot conn=%" PRIu32 " active=%d max=%d", connection_id, tunnel_active_count(), MAX_TUNNELS);
        relay_send_text_frame(relay_fd, FRAME_ERROR, connection_id, "no tunnel slot");
        relay_send_frame(relay_fd, FRAME_CLOSE, connection_id, NULL, 0);
        return;
    }

    tcp_close_fd(&tunnel->fd);
    tunnel_free_pending(tunnel);
    tunnel->state = TUNNEL_CONNECTING;
    tunnel->connect_started_ms = now_ms();
    ESP_LOGI(TAG, "connect terminal begin conn=%" PRIu32 " target=%s:%u", connection_id, host, port);

    bool connected = false;
    if (tcp_connect_begin(host, port, MAX_FRAME_PAYLOAD * 2, &tunnel->fd, &connected) != ESP_OK) {
        ESP_LOGE(TAG, "terminal connection start failed conn=%" PRIu32 " target=%s:%u", connection_id, host, port);
        relay_send_text_frame(relay_fd, FRAME_ERROR, connection_id, "terminal connection failed");
        relay_send_frame(relay_fd, FRAME_CLOSE, connection_id, NULL, 0);
        tunnel_close_reason(connection_id, "terminal connection start failed");
        return;
    }

    if (connected) {
        tunnel->state = TUNNEL_CONNECTED;
        ESP_LOGI(TAG, "terminal connected conn=%" PRIu32 " elapsed=%" PRIi64 " ms", connection_id, now_ms() - tunnel->connect_started_ms);
    }
    tunnel->last_activity_ms = now_ms();
}

static esp_err_t tunnel_buffer_pending_data(tunnel_connection_t *tunnel, const uint8_t *payload, uint32_t length)
{
    if ((size_t)length > CONFIG_REMOTE_AGENT_PENDING_TX_BUFFER ||
        tunnel->pending_tx_len + (size_t)length > CONFIG_REMOTE_AGENT_PENDING_TX_BUFFER) {
        return ESP_ERR_NO_MEM;
    }

    size_t needed = tunnel->pending_tx_len + (size_t)length;
    if (needed > tunnel->pending_tx_capacity) {
        size_t next_capacity = tunnel->pending_tx_capacity == 0 ? 1024 : tunnel->pending_tx_capacity;
        while (next_capacity < needed) {
            next_capacity *= 2;
        }

        uint8_t *next = realloc(tunnel->pending_tx, next_capacity);
        if (!next) {
            return ESP_ERR_NO_MEM;
        }
        tunnel->pending_tx = next;
        tunnel->pending_tx_capacity = next_capacity;
    }

    memcpy(tunnel->pending_tx + tunnel->pending_tx_len, payload, length);
    tunnel->pending_tx_len += length;
    return ESP_OK;
}

static esp_err_t tunnel_flush_pending_data(tunnel_connection_t *tunnel)
{
    if (tunnel->pending_tx_len == 0) {
        return ESP_OK;
    }

    esp_err_t err = tcp_write_all(tunnel->fd, tunnel->pending_tx, tunnel->pending_tx_len);
    if (err == ESP_OK) {
        s_bytes_from_server += tunnel->pending_tx_len;
        status_led_note_data();
        tunnel_free_pending(tunnel);
    }
    return err;
}

void tunnel_handle_data(int relay_fd, uint32_t connection_id, const uint8_t *payload, uint32_t length)
{
    tunnel_connection_t *tunnel = find_tunnel(connection_id);
    if (!tunnel || tunnel->fd < 0) {
        ESP_LOGW(TAG, "data for missing tunnel conn=%" PRIu32, connection_id);
        relay_send_frame(relay_fd, FRAME_CLOSE, connection_id, NULL, 0);
        tunnel_close_reason(connection_id, "data for missing tunnel");
        return;
    }

    if (tunnel->state == TUNNEL_CONNECTING) {
        if (tunnel_buffer_pending_data(tunnel, payload, length) != ESP_OK) {
            ESP_LOGW(TAG, "pending data overflow conn=%" PRIu32 " pending=%u incoming=%" PRIu32,
                     connection_id, (unsigned)tunnel->pending_tx_len, length);
            relay_send_text_frame(relay_fd, FRAME_ERROR, connection_id, "pending data overflow");
            relay_send_frame(relay_fd, FRAME_CLOSE, connection_id, NULL, 0);
            tunnel_close_reason(connection_id, "pending data overflow");
            return;
        }
        tunnel->last_activity_ms = now_ms();
        return;
    }

    if (tcp_write_all(tunnel->fd, payload, length) != ESP_OK) {
        ESP_LOGW(TAG, "terminal write failed conn=%" PRIu32 " len=%" PRIu32, connection_id, length);
        relay_send_text_frame(relay_fd, FRAME_ERROR, connection_id, "terminal write failed");
        relay_send_frame(relay_fd, FRAME_CLOSE, connection_id, NULL, 0);
        tunnel_close_reason(connection_id, "terminal write failed");
        return;
    }
    s_bytes_from_server += length;
    status_led_note_data();
    tunnel->last_activity_ms = now_ms();
}

esp_err_t tunnel_pump_terminal_traffic(int relay_fd)
{
    fd_set readfds;
    fd_set writefds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    int max_fd = -1;

    for (int i = 0; i < MAX_TUNNELS; i++) {
        if (!s_tunnels[i].active || s_tunnels[i].fd < 0) {
            continue;
        }

        if (s_tunnels[i].state == TUNNEL_CONNECTING) {
            int64_t elapsed_ms = now_ms() - s_tunnels[i].connect_started_ms;
            if (elapsed_ms > CONFIG_REMOTE_AGENT_TERMINAL_CONNECT_TIMEOUT_MS) {
                ESP_LOGW(TAG, "terminal connection timeout conn=%" PRIu32 " elapsed=%" PRIi64 " ms pending=%u",
                         s_tunnels[i].id, elapsed_ms, (unsigned)s_tunnels[i].pending_tx_len);
                relay_send_text_frame(relay_fd, FRAME_ERROR, s_tunnels[i].id, "terminal connection timeout");
                relay_send_frame(relay_fd, FRAME_CLOSE, s_tunnels[i].id, NULL, 0);
                tunnel_close_reason(s_tunnels[i].id, "terminal connection timeout");
                continue;
            }
            FD_SET(s_tunnels[i].fd, &writefds);
        } else {
            FD_SET(s_tunnels[i].fd, &readfds);
        }

        if (s_tunnels[i].fd > max_fd) {
            max_fd = s_tunnels[i].fd;
        }
    }

    if (max_fd < 0) {
        return ESP_OK;
    }

    struct timeval tv = {.tv_sec = 0, .tv_usec = 0};
    int selected = select(max_fd + 1, &readfds, &writefds, NULL, &tv);
    if (selected <= 0) {
        return ESP_OK;
    }

    for (int i = 0; i < MAX_TUNNELS; i++) {
        tunnel_connection_t *tunnel = &s_tunnels[i];
        if (!tunnel->active || tunnel->fd < 0) {
            continue;
        }

        if (tunnel->state == TUNNEL_CONNECTING && FD_ISSET(tunnel->fd, &writefds)) {
            int socket_error = 0;
            if (tcp_connect_finish(tunnel->fd, &socket_error) != ESP_OK) {
                ESP_LOGE(TAG, "terminal connection failed conn=%" PRIu32 " errno=%d elapsed=%" PRIi64 " ms",
                         tunnel->id, socket_error, now_ms() - tunnel->connect_started_ms);
                relay_send_text_frame(relay_fd, FRAME_ERROR, tunnel->id, "terminal connection failed");
                relay_send_frame(relay_fd, FRAME_CLOSE, tunnel->id, NULL, 0);
                tunnel_close_reason(tunnel->id, "terminal connection failed");
                continue;
            }

            tunnel->state = TUNNEL_CONNECTED;
            ESP_LOGI(TAG, "terminal connected conn=%" PRIu32 " elapsed=%" PRIi64 " ms pending=%u",
                     tunnel->id, now_ms() - tunnel->connect_started_ms, (unsigned)tunnel->pending_tx_len);
            if (tunnel_flush_pending_data(tunnel) != ESP_OK) {
                ESP_LOGW(TAG, "terminal pending write failed conn=%" PRIu32, tunnel->id);
                relay_send_text_frame(relay_fd, FRAME_ERROR, tunnel->id, "terminal write failed");
                relay_send_frame(relay_fd, FRAME_CLOSE, tunnel->id, NULL, 0);
                tunnel_close_reason(tunnel->id, "terminal pending write failed");
                continue;
            }
            tunnel->last_activity_ms = now_ms();
            continue;
        }

        if (tunnel->state != TUNNEL_CONNECTED || !FD_ISSET(tunnel->fd, &readfds)) {
            continue;
        }

        int received = recv(tunnel->fd, s_pipe_buffer, sizeof(s_pipe_buffer), 0);
        if (received <= 0) {
            ESP_LOGI(TAG, "terminal closed conn=%" PRIu32 " recv=%d", tunnel->id, received);
            relay_send_frame(relay_fd, FRAME_CLOSE, tunnel->id, NULL, 0);
            tunnel_close_reason(tunnel->id, "terminal closed");
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
