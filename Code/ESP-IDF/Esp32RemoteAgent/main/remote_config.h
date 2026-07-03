#pragma once

#include <stdint.h>

#include "esp_err.h"

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

esp_err_t remote_config_load(remote_config_t *cfg);
