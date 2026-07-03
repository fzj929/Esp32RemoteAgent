#include "tcp_utils.h"

#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_log.h"
#include "lwip/netdb.h"
#include "lwip/tcp.h"

static const char *TAG = "TcpUtils";

void tcp_close_fd(int *fd)
{
    if (*fd >= 0) {
        shutdown(*fd, SHUT_RDWR);
        close(*fd);
        *fd = -1;
    }
}

esp_err_t tcp_write_all(int fd, const uint8_t *buffer, size_t length)
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

int tcp_connect_host(const char *host, uint16_t port, int timeout_ms, int socket_buffer_size)
{
    char port_text[8];
    snprintf(port_text, sizeof(port_text), "%u", port);

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
    };
    struct addrinfo *results = NULL;
    int gai_err = getaddrinfo(host, port_text, &hints, &results);
    if (gai_err != 0 || results == NULL) {
        ESP_LOGE(TAG, "resolve %s:%u failed err=%d", host, port, gai_err);
        return -1;
    }

    int fd = -1;
    for (const struct addrinfo *it = results; it != NULL; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            ESP_LOGE(TAG, "socket failed errno=%d", errno);
            continue;
        }

        struct timeval tv = {
            .tv_sec = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        int yes = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

        if (socket_buffer_size > 0) {
            setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &socket_buffer_size, sizeof(socket_buffer_size));
            setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &socket_buffer_size, sizeof(socket_buffer_size));
        }

        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            freeaddrinfo(results);
            return fd;
        }

        ESP_LOGE(TAG, "connect %s:%u failed errno=%d", host, port, errno);
        close(fd);
        fd = -1;
    }

    freeaddrinfo(results);
    return -1;
}
