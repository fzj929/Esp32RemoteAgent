#include "relay_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/md.h"

static const char *TAG = "RelayProtocol";

static void write_u32_be(uint8_t *dst, uint32_t value)
{
    dst[0] = (value >> 24) & 0xff;
    dst[1] = (value >> 16) & 0xff;
    dst[2] = (value >> 8) & 0xff;
    dst[3] = value & 0xff;
}

uint32_t relay_read_u32_be(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) | ((uint32_t)src[2] << 8) | src[3];
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
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

esp_err_t relay_read_exact(int fd, uint8_t *buffer, size_t length, int timeout_ms)
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

esp_err_t relay_send_frame(int relay_fd, uint8_t type, uint32_t connection_id, const uint8_t *payload, uint32_t length)
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

esp_err_t relay_send_text_frame(int relay_fd, uint8_t type, uint32_t connection_id, const char *text)
{
    return relay_send_frame(relay_fd, type, connection_id, (const uint8_t *)text, strlen(text));
}

bool relay_extract_json_string(const char *json, const char *key, char *output, size_t output_size)
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

bool relay_extract_json_u16(const char *json, const char *key, uint16_t *output)
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

esp_err_t relay_hmac_sha256_hex(const char *key, const char *payload, char *output, size_t output_size)
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
