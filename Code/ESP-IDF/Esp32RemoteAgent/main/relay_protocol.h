#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

enum {
    FRAME_REGISTER = 1,
    FRAME_REGISTER_ACK = 2,
    FRAME_HEARTBEAT = 3,
    FRAME_OPEN = 4,
    FRAME_DATA = 5,
    FRAME_CLOSE = 6,
    FRAME_ERROR = 7,
    FRAME_HEADER_LEN = 9,
    MAX_FRAME_PAYLOAD = 8192,
};

uint32_t relay_read_u32_be(const uint8_t *src);
esp_err_t relay_read_exact(int fd, uint8_t *buffer, size_t length, int timeout_ms);
esp_err_t relay_send_frame(int relay_fd, uint8_t type, uint32_t connection_id, const uint8_t *payload, uint32_t length);
esp_err_t relay_send_text_frame(int relay_fd, uint8_t type, uint32_t connection_id, const char *text);
bool relay_extract_json_string(const char *json, const char *key, char *output, size_t output_size);
bool relay_extract_json_u16(const char *json, const char *key, uint16_t *output);
esp_err_t relay_hmac_sha256_hex(const char *key, const char *payload, char *output, size_t output_size);
