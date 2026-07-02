#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "lwip/esp_netif_net_stack.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/tcp.h"
#include "mbedtls/md.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tusb.h"
#include "tinyusb.h"
#include "tinyusb_net.h"

static const char *TAG = "RemoteAgent";
static const char *TAG_USB = "USB_NCM";

// ===== Factory configuration =====
static const char *DEFAULT_WIFI_SSID = CONFIG_REMOTE_AGENT_WIFI_SSID;
static const char *DEFAULT_WIFI_PASSWORD = CONFIG_REMOTE_AGENT_WIFI_PASSWORD;
static const char *DEFAULT_SERVER_HOST = CONFIG_REMOTE_AGENT_SERVER_HOST;
static const uint16_t DEFAULT_SERVER_CONTROL_PORT = CONFIG_REMOTE_AGENT_SERVER_CONTROL_PORT;
static const char *DEFAULT_BOARD_ID = CONFIG_REMOTE_AGENT_BOARD_ID;
static const char *DEFAULT_BOARD_KEY = CONFIG_REMOTE_AGENT_BOARD_KEY;
static const uint16_t DEFAULT_ASSIGNED_PUBLIC_PORT = CONFIG_REMOTE_AGENT_ASSIGNED_PUBLIC_PORT;
static const char *DEFAULT_TERMINAL_RDP_HOST = CONFIG_REMOTE_AGENT_TERMINAL_RDP_HOST;
static const uint16_t DEFAULT_TERMINAL_RDP_PORT = CONFIG_REMOTE_AGENT_TERMINAL_RDP_PORT;
static const char *FIRMWARE_VERSION = "esp-idf-s3-0.2.0";

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char server_host[64];
    uint16_t server_control_port;
    char board_id[32];
    char board_key[96];
    uint16_t assigned_public_port;
    char terminal_rdp_host[64];
    uint16_t terminal_rdp_port;
} remote_config_t;

// ===== Relay protocol =====
static const uint8_t FRAME_REGISTER = 1;
static const uint8_t FRAME_REGISTER_ACK = 2;
static const uint8_t FRAME_HEARTBEAT = 3;
static const uint8_t FRAME_OPEN = 4;
static const uint8_t FRAME_DATA = 5;
static const uint8_t FRAME_CLOSE = 6;
static const uint8_t FRAME_ERROR = 7;

enum {
    FRAME_HEADER_LEN = 9,
    MAX_FRAME_PAYLOAD = 8192,
    HEARTBEAT_INTERVAL_MS = 15000,
    RECONNECT_DELAY_MS = 3000,
    MAX_TUNNELS = 4,
};

typedef struct {
    uint32_t id;
    int fd;
    bool active;
    int64_t last_activity_ms;
} tunnel_connection_t;

static EventGroupHandle_t s_wifi_event_group;
static const EventBits_t WIFI_CONNECTED_BIT = BIT0;
static tunnel_connection_t s_tunnels[MAX_TUNNELS];
static remote_config_t s_config;
static uint8_t s_rx_buffer[MAX_FRAME_PAYLOAD];
static uint8_t s_pipe_buffer[MAX_FRAME_PAYLOAD];
static int64_t s_last_heartbeat_ms;
static uint64_t s_bytes_from_server;
static uint64_t s_bytes_from_terminal;
static uint16_t s_runtime_assigned_public_port;
static esp_netif_t *s_usb_netif;
static led_strip_handle_t s_status_led;
static bool s_status_led_ready;
static bool s_relay_online;
static int64_t s_data_flash_until_ms;

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
    // Set header: length, type, windows version, total length
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

    // Configuration subset header: length, type, configuration index, reserved, configuration total length
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),

    // Function Subset header: length, type, first interface, reserved, subset length
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), NCM_INTERFACE_NUMBER, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),

    // MS OS 2.0 Compatible ID descriptor: length, type, compatible ID, sub compatible ID
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'N', 'C', 'M', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // MS OS 2.0 Registry property descriptor: length, type
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A),
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00,
    'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, 0x00, 0x00,
    U16_TO_U8S_LE(0x0050),
    // bPropertyData: {12345678-0D08-43FD-8B3E-127CA8AFFF9D}
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

static void write_u32_be(uint8_t *dst, uint32_t value)
{
    dst[0] = (value >> 24) & 0xff;
    dst[1] = (value >> 16) & 0xff;
    dst[2] = (value >> 8) & 0xff;
    dst[3] = value & 0xff;
}

static uint32_t read_u32_be(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) | ((uint32_t)src[2] << 8) | src[3];
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

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

static esp_err_t config_load(void)
{
    config_set_defaults(&s_config);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("remote_cfg", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open remote_cfg failed, using compile defaults: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(config_save_defaults_if_missing(nvs, &s_config), TAG, "save default config failed");
    nvs_get_string_or_keep(nvs, "wifi_ssid", s_config.wifi_ssid, sizeof(s_config.wifi_ssid));
    nvs_get_string_or_keep(nvs, "wifi_pass", s_config.wifi_password, sizeof(s_config.wifi_password));
    nvs_get_string_or_keep(nvs, "server", s_config.server_host, sizeof(s_config.server_host));
    nvs_get_u16_or_keep(nvs, "ctrl_port", &s_config.server_control_port);
    nvs_get_string_or_keep(nvs, "board_id", s_config.board_id, sizeof(s_config.board_id));
    nvs_get_string_or_keep(nvs, "board_key", s_config.board_key, sizeof(s_config.board_key));
    nvs_get_u16_or_keep(nvs, "public_port", &s_config.assigned_public_port);
    nvs_get_string_or_keep(nvs, "target_host", s_config.terminal_rdp_host, sizeof(s_config.terminal_rdp_host));
    nvs_get_u16_or_keep(nvs, "target_port", &s_config.terminal_rdp_port);
    nvs_close(nvs);
    return ESP_OK;
}

static void bytes_to_hex(const uint8_t *bytes, size_t length, char *output, size_t output_size)
{
    static const char hex[] = "0123456789abcdef";
    if (output_size < length * 2 + 1) {
        if (output_size > 0) {
            output[0] = '\0';
        }
        return;
    }

    for (size_t i = 0; i < length; i++) {
        output[i * 2] = hex[(bytes[i] >> 4) & 0x0f];
        output[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    output[length * 2] = '\0';
}

static esp_err_t hmac_sha256_hex(const char *key, const char *payload, char *output, size_t output_size)
{
    uint8_t digest[32];
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    ESP_RETURN_ON_FALSE(info != NULL, ESP_FAIL, TAG, "sha256 info unavailable");
    int rc = mbedtls_md_hmac(info,
                             (const unsigned char *)key, strlen(key),
                             (const unsigned char *)payload, strlen(payload),
                             digest);
    ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, TAG, "hmac failed");
    bytes_to_hex(digest, sizeof(digest), output, output_size);
    return ESP_OK;
}

static uint8_t led_scale(uint8_t value)
{
#if CONFIG_REMOTE_AGENT_STATUS_LED_ENABLED
    return (uint8_t)((uint16_t)value * CONFIG_REMOTE_AGENT_STATUS_LED_BRIGHTNESS / 255);
#else
    return 0;
#endif
}

static void status_led_apply(uint8_t red, uint8_t green, uint8_t blue)
{
#if CONFIG_REMOTE_AGENT_STATUS_LED_ENABLED
    if (!s_status_led_ready) {
        return;
    }

    led_strip_set_pixel(s_status_led, 0, led_scale(red), led_scale(green), led_scale(blue));
    led_strip_refresh(s_status_led);
#else
    (void)red;
    (void)green;
    (void)blue;
#endif
}

static void status_led_set_disconnected(void)
{
    status_led_apply(255, 0, 0);
}

static void status_led_set_connected(void)
{
    status_led_apply(0, 255, 0);
}

static void status_led_note_data(void)
{
    s_data_flash_until_ms = now_ms() + 80;
    status_led_apply(0, 0, 255);
}

static void status_led_tick(void)
{
    if (s_data_flash_until_ms > 0 && now_ms() >= s_data_flash_until_ms) {
        s_data_flash_until_ms = 0;
        if (s_relay_online) {
            status_led_set_connected();
        } else {
            status_led_set_disconnected();
        }
    }
}

static esp_err_t status_led_init(void)
{
#if CONFIG_REMOTE_AGENT_STATUS_LED_ENABLED
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_REMOTE_AGENT_STATUS_LED_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_status_led);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "status LED init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_status_led_ready = true;
    led_strip_clear(s_status_led);
    status_led_set_disconnected();
#endif
    return ESP_OK;
}

static void close_fd(int *fd)
{
    if (*fd >= 0) {
        shutdown(*fd, SHUT_RDWR);
        close(*fd);
        *fd = -1;
    }
}

static esp_err_t read_exact(int fd, uint8_t *buffer, size_t length, int timeout_ms)
{
    size_t offset = 0;
    int64_t deadline = now_ms() + timeout_ms;

    while (offset < length) {
        int remaining = (int)(deadline - now_ms());
        if (remaining <= 0) {
            return ESP_ERR_TIMEOUT;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        struct timeval tv = {
            .tv_sec = remaining / 1000,
            .tv_usec = (remaining % 1000) * 1000,
        };

        int selected = select(fd + 1, &readfds, NULL, NULL, &tv);
        if (selected < 0) {
            return ESP_FAIL;
        }
        if (selected == 0) {
            return ESP_ERR_TIMEOUT;
        }

        int received = recv(fd, buffer + offset, length - offset, 0);
        if (received <= 0) {
            return ESP_FAIL;
        }
        offset += received;
    }

    return ESP_OK;
}

static esp_err_t write_all(int fd, const uint8_t *buffer, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        int sent = send(fd, buffer + offset, length - offset, 0);
        if (sent <= 0) {
            return ESP_FAIL;
        }
        offset += sent;
    }
    return ESP_OK;
}

static esp_err_t send_frame(int relay_fd, uint8_t type, uint32_t connection_id, const uint8_t *payload, uint32_t length)
{
    uint8_t header[FRAME_HEADER_LEN];
    header[0] = type;
    write_u32_be(header + 1, connection_id);
    write_u32_be(header + 5, length);

    ESP_RETURN_ON_ERROR(write_all(relay_fd, header, sizeof(header)), TAG, "send frame header failed");
    if (length > 0) {
        ESP_RETURN_ON_ERROR(write_all(relay_fd, payload, length), TAG, "send frame payload failed");
    }
    return ESP_OK;
}

static esp_err_t send_text_frame(int relay_fd, uint8_t type, uint32_t connection_id, const char *text)
{
    return send_frame(relay_fd, type, connection_id, (const uint8_t *)text, strlen(text));
}

static bool extract_json_string(const char *json, const char *key, char *output, size_t output_size)
{
    if (!json || !key || !output || output_size == 0) {
        return false;
    }

    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *key_pos = strstr(json, pattern);
    if (!key_pos) {
        return false;
    }

    const char *colon = strchr(key_pos + strlen(pattern), ':');
    if (!colon) {
        return false;
    }

    const char *start = strchr(colon, '"');
    if (!start) {
        return false;
    }
    start++;

    const char *end = strchr(start, '"');
    if (!end) {
        return false;
    }

    size_t len = (size_t)(end - start);
    if (len >= output_size) {
        len = output_size - 1;
    }
    memcpy(output, start, len);
    output[len] = '\0';
    return true;
}

static bool extract_json_u16(const char *json, const char *key, uint16_t *output)
{
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *key_pos = strstr(json, pattern);
    if (!key_pos) {
        return false;
    }

    const char *colon = strchr(key_pos + strlen(pattern), ':');
    if (!colon) {
        return false;
    }

    char *end = NULL;
    unsigned long value = strtoul(colon + 1, &end, 10);
    if (end == colon + 1 || value > 65535) {
        return false;
    }

    *output = (uint16_t)value;
    return true;
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

static void close_tunnel(uint32_t id)
{
    tunnel_connection_t *tunnel = find_tunnel(id);
    if (!tunnel) {
        return;
    }

    ESP_LOGI(TAG, "close tunnel id=%" PRIu32, id);
    close_fd(&tunnel->fd);
    tunnel->active = false;
    tunnel->id = 0;
}

static int connect_tcp_host(const char *host, uint16_t port, int timeout_ms)
{
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        ESP_LOGE(TAG, "only IPv4 literal hosts are currently supported: %s", host);
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) {
        ESP_LOGE(TAG, "socket failed errno=%d", errno);
        return -1;
    }

    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    int socket_buffer = MAX_FRAME_PAYLOAD * 2;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &socket_buffer, sizeof(socket_buffer));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &socket_buffer, sizeof(socket_buffer));

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "connect %s:%u failed errno=%d", host, port, errno);
        close(fd);
        return -1;
    }

    return fd;
}

static esp_err_t register_board(int relay_fd)
{
    const uint16_t requested_assigned_port = 0;
    char nonce[18];
    snprintf(nonce, sizeof(nonce), "%08" PRIx32 "%08" PRIx32, esp_random(), esp_random());
    int64_t timestamp_ms = now_ms();

    char auth_payload[256];
    snprintf(auth_payload, sizeof(auth_payload), "%s|%u|%s|%u|%s|%s|%" PRIi64,
             s_config.board_id,
             requested_assigned_port,
             s_config.terminal_rdp_host,
             s_config.terminal_rdp_port,
             FIRMWARE_VERSION,
             nonce,
             timestamp_ms);

    char signature[65];
    ESP_RETURN_ON_ERROR(hmac_sha256_hex(s_config.board_key, auth_payload, signature, sizeof(signature)), TAG, "auth signature failed");

    char json[640];
    snprintf(json, sizeof(json),
             "{\"boardId\":\"%s\",\"assignedPort\":%u,"
             "\"targetHost\":\"%s\",\"targetPort\":%u,\"firmware\":\"%s\","
             "\"authNonce\":\"%s\",\"authTimestampMs\":%" PRIi64 ",\"authSignature\":\"%s\"}",
             s_config.board_id,
             requested_assigned_port,
             s_config.terminal_rdp_host,
             s_config.terminal_rdp_port,
             FIRMWARE_VERSION,
             nonce,
             timestamp_ms,
             signature);

    ESP_LOGI(TAG, "register boardId=%s assignedPort=request-server target=%s:%u",
             s_config.board_id, s_config.terminal_rdp_host, s_config.terminal_rdp_port);
    return send_text_frame(relay_fd, FRAME_REGISTER, 0, json);
}

static esp_err_t wait_register_ack(int relay_fd)
{
    uint8_t header[FRAME_HEADER_LEN];
    ESP_RETURN_ON_ERROR(read_exact(relay_fd, header, sizeof(header), 5000), TAG, "registration ack timeout");

    uint8_t type = header[0];
    uint32_t len = read_u32_be(header + 5);
    if (len > MAX_FRAME_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (len > 0) {
        ESP_RETURN_ON_ERROR(read_exact(relay_fd, s_rx_buffer, len, 5000), TAG, "registration ack payload timeout");
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
        if (extract_json_u16(ack_json, "assignedPort", &assigned_port) && assigned_port > 0) {
            s_runtime_assigned_public_port = assigned_port;
        }

        char target_host[sizeof(s_config.terminal_rdp_host)];
        if (extract_json_string(ack_json, "targetHost", target_host, sizeof(target_host))) {
            strlcpy(s_config.terminal_rdp_host, target_host, sizeof(s_config.terminal_rdp_host));
        }

        uint16_t target_port = 0;
        if (extract_json_u16(ack_json, "targetPort", &target_port) && target_port > 0) {
            s_config.terminal_rdp_port = target_port;
        }
    }

    ESP_LOGI(TAG, "registered successfully assignedPort=%u target=%s:%u",
             s_runtime_assigned_public_port, s_config.terminal_rdp_host, s_config.terminal_rdp_port);
    s_last_heartbeat_ms = now_ms();
    return ESP_OK;
}

static void handle_open(int relay_fd, uint32_t connection_id, const uint8_t *payload, uint32_t length)
{
    char json[256];
    size_t copy_len = length < sizeof(json) - 1 ? length : sizeof(json) - 1;
    memcpy(json, payload, copy_len);
    json[copy_len] = '\0';

    char host[64];
    strlcpy(host, s_config.terminal_rdp_host, sizeof(host));
    uint16_t port = s_config.terminal_rdp_port;
    extract_json_string(json, "host", host, sizeof(host));
    extract_json_u16(json, "port", &port);

    tunnel_connection_t *tunnel = allocate_tunnel(connection_id);
    if (!tunnel) {
        send_text_frame(relay_fd, FRAME_ERROR, connection_id, "no tunnel slot");
        send_frame(relay_fd, FRAME_CLOSE, connection_id, NULL, 0);
        return;
    }

    close_fd(&tunnel->fd);
    ESP_LOGI(TAG, "connect terminal conn=%" PRIu32 " target=%s:%u", connection_id, host, port);
    tunnel->fd = connect_tcp_host(host, port, 3000);
    if (tunnel->fd < 0) {
        ESP_LOGE(TAG, "terminal connection failed conn=%" PRIu32 " target=%s:%u", connection_id, host, port);
        send_text_frame(relay_fd, FRAME_ERROR, connection_id, "terminal connection failed");
        send_frame(relay_fd, FRAME_CLOSE, connection_id, NULL, 0);
        close_tunnel(connection_id);
        return;
    }

    tunnel->last_activity_ms = now_ms();
    ESP_LOGI(TAG, "terminal connected conn=%" PRIu32, connection_id);
}

static void handle_data(int relay_fd, uint32_t connection_id, const uint8_t *payload, uint32_t length)
{
    tunnel_connection_t *tunnel = find_tunnel(connection_id);
    if (!tunnel || tunnel->fd < 0) {
        send_frame(relay_fd, FRAME_CLOSE, connection_id, NULL, 0);
        close_tunnel(connection_id);
        return;
    }

    if (write_all(tunnel->fd, payload, length) != ESP_OK) {
        send_text_frame(relay_fd, FRAME_ERROR, connection_id, "terminal write failed");
        send_frame(relay_fd, FRAME_CLOSE, connection_id, NULL, 0);
        close_tunnel(connection_id);
        return;
    }
    s_bytes_from_server += length;
    status_led_note_data();
    tunnel->last_activity_ms = now_ms();
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
    esp_err_t err = read_exact(relay_fd, header, sizeof(header), 5000);
    ESP_RETURN_ON_ERROR(err, TAG, "read relay frame header failed");

    uint8_t type = header[0];
    uint32_t connection_id = read_u32_be(header + 1);
    uint32_t length = read_u32_be(header + 5);
    if (length > MAX_FRAME_PAYLOAD) {
        ESP_LOGE(TAG, "relay payload too large type=%u conn=%" PRIu32 " len=%" PRIu32, type, connection_id, length);
        return ESP_FAIL;
    }

    if (length > 0) {
        ESP_RETURN_ON_ERROR(read_exact(relay_fd, s_rx_buffer, length, 5000), TAG, "read relay payload failed");
    }

    switch (type) {
    case FRAME_OPEN:
        handle_open(relay_fd, connection_id, s_rx_buffer, length);
        break;
    case FRAME_DATA:
        handle_data(relay_fd, connection_id, s_rx_buffer, length);
        break;
    case FRAME_CLOSE:
        close_tunnel(connection_id);
        break;
    default:
        ESP_LOGW(TAG, "unknown relay frame type=%u conn=%" PRIu32 " len=%" PRIu32, type, connection_id, length);
        break;
    }

    return ESP_OK;
}

static esp_err_t pump_terminal_traffic(int relay_fd)
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
            send_frame(relay_fd, FRAME_CLOSE, tunnel->id, NULL, 0);
            close_tunnel(tunnel->id);
            continue;
        }

        if (send_frame(relay_fd, FRAME_DATA, tunnel->id, s_pipe_buffer, received) != ESP_OK) {
            return ESP_FAIL;
        }
        s_bytes_from_terminal += received;
        status_led_note_data();
        tunnel->last_activity_ms = now_ms();
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

    int active_tunnels = 0;
    for (int i = 0; i < MAX_TUNNELS; i++) {
        if (s_tunnels[i].active) {
            active_tunnels++;
        }
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
             active_tunnels,
             s_bytes_from_server,
             s_bytes_from_terminal,
             FIRMWARE_VERSION,
             s_runtime_assigned_public_port);
    if (send_text_frame(relay_fd, FRAME_HEARTBEAT, 0, json) == ESP_OK) {
        ESP_LOGI(TAG, "heartbeat sent freeHeap=%lu", (unsigned long)esp_get_free_heap_size());
    }
    s_last_heartbeat_ms = now_ms();
}

static void relay_task(void *arg)
{
    while (true) {
        xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

        ESP_LOGI(TAG, "connecting relay %s:%u", s_config.server_host, s_config.server_control_port);
        int relay_fd = connect_tcp_host(s_config.server_host, s_config.server_control_port, 5000);
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
            if (pump_terminal_traffic(relay_fd) != ESP_OK) {
                break;
            }
            send_heartbeat_if_needed(relay_fd);
            status_led_tick();
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        ESP_LOGW(TAG, "relay disconnected, close active tunnels");
        close(relay_fd);
        s_relay_online = false;
        status_led_set_disconnected();
        for (int i = 0; i < MAX_TUNNELS; i++) {
            if (s_tunnels[i].active) {
                close_tunnel(s_tunnels[i].id);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
    }
}

static esp_err_t usb_netif_transmit(void *h, void *buffer, size_t len)
{
    esp_err_t err = tinyusb_net_send_sync(buffer, len, NULL, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG_USB, "USB send failed: %s", esp_err_to_name(err));
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

static esp_err_t startUsbNetwork(void)
{
    ESP_LOGI(TAG_USB, "USB NCM device initialization");

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &s_usb_device_desc,
        .external_phy = false,
    };
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG_USB, "Failed to install TinyUSB driver");

    const tinyusb_net_config_t net_config = {
        .mac_addr = {0x02, 0x02, 0x11, 0x22, 0x33, 0x01},
        .on_recv_callback = usb_recv_callback,
    };
    ESP_RETURN_ON_ERROR(tinyusb_net_init(TINYUSB_USBDEV_0, &net_config), TAG_USB, "Failed to initialize USB NCM device");

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
    ESP_RETURN_ON_FALSE(s_usb_netif != NULL, ESP_FAIL, TAG_USB, "Failed to create USB NCM netif");

    uint8_t lwip_mac[6] = {0x02, 0x02, 0x11, 0x22, 0x33, 0x02};
    ESP_RETURN_ON_ERROR(esp_netif_set_mac(s_usb_netif, lwip_mac), TAG_USB, "Failed to set USB netif MAC");

    esp_netif_action_start(s_usb_netif, 0, 0, 0);
    ESP_LOGI(TAG_USB, "USB NCM ready: ESP32 IP 192.168.77.1/24, terminal can use DHCP or 192.168.77.2/24");
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting");
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected ip=" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t start_wifi(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_event_group != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create WiFi event group");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop init failed");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "WiFi init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), TAG, "WiFi handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL), TAG, "IP handler failed");

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, s_config.wifi_ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, s_config.wifi_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "WiFi set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "WiFi set config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "WiFi start failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "WiFi power save disable failed");
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 remote RDP agent starting");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(config_load());
    s_runtime_assigned_public_port = 0;
    ESP_LOGI(TAG, "boardId=%s assignedPort=server-assigned server=%s:%u",
             s_config.board_id, s_config.server_host, s_config.server_control_port);
    ESP_ERROR_CHECK(status_led_init());

    for (int i = 0; i < MAX_TUNNELS; i++) {
        s_tunnels[i].fd = -1;
    }

    ESP_ERROR_CHECK(start_wifi());
    ESP_ERROR_CHECK(startUsbNetwork());

    xTaskCreatePinnedToCore(relay_task, "relay_task", 8192, NULL, 5, NULL, 1);
}
