#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

int tcp_connect_host(const char *host, uint16_t port, int timeout_ms, int socket_buffer_size);
esp_err_t tcp_connect_begin(const char *host, uint16_t port, int socket_buffer_size, int *fd, bool *connected);
esp_err_t tcp_connect_finish(int fd, int *socket_error);
void tcp_close_fd(int *fd);
esp_err_t tcp_write_all(int fd, const uint8_t *buffer, size_t length);
