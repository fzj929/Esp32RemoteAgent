#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "remote_config.h"

void tunnel_init(void);
void tunnel_close(uint32_t id);
void tunnel_close_all(void);
int tunnel_active_count(void);
uint64_t tunnel_bytes_from_server(void);
uint64_t tunnel_bytes_from_terminal(void);
void tunnel_handle_open(int relay_fd, uint32_t connection_id, const uint8_t *payload, uint32_t length, const remote_config_t *config);
void tunnel_handle_data(int relay_fd, uint32_t connection_id, const uint8_t *payload, uint32_t length);
esp_err_t tunnel_pump_terminal_traffic(int relay_fd);
