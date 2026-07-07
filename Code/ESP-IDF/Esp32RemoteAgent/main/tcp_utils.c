#include "tcp_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_log.h"
#include "lwip/netdb.h"
#include "lwip/tcp.h"

static const char *TAG = "TcpUtils";

static esp_err_t tcp_set_blocking(int fd, bool blocking)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return ESP_FAIL;
    }

    if (blocking) {
        flags &= ~O_NONBLOCK;
    } else {
        flags |= O_NONBLOCK;
    }

    return fcntl(fd, F_SETFL, flags) == 0 ? ESP_OK : ESP_FAIL;
}

static void tcp_configure_socket(int fd, int timeout_ms, int socket_buffer_size)
{
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
}

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

        tcp_configure_socket(fd, timeout_ms, socket_buffer_size);

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

esp_err_t tcp_connect_begin(const char *host, uint16_t port, int socket_buffer_size, int *fd, bool *connected)
{
    *fd = -1;
    *connected = false;

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
        return ESP_FAIL;
    }

    esp_err_t result = ESP_FAIL;
    for (const struct addrinfo *it = results; it != NULL; it = it->ai_next) {
        int sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "socket failed errno=%d", errno);
            continue;
        }

        tcp_configure_socket(sock, 1000, socket_buffer_size);
        if (tcp_set_blocking(sock, false) != ESP_OK) {
            ESP_LOGE(TAG, "set nonblocking failed errno=%d", errno);
            close(sock);
            continue;
        }

        if (connect(sock, it->ai_addr, it->ai_addrlen) == 0) {
            tcp_set_blocking(sock, true);
            *fd = sock;
            *connected = true;
            result = ESP_OK;
            break;
        }

        if (errno == EINPROGRESS) {
            *fd = sock;
            *connected = false;
            result = ESP_OK;
            break;
        }

        ESP_LOGE(TAG, "connect %s:%u failed errno=%d", host, port, errno);
        close(sock);
    }

    freeaddrinfo(results);
    return result;
}

esp_err_t tcp_connect_finish(int fd, int *socket_error)
{
    int err = 0;
    socklen_t err_len = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0) {
        *socket_error = errno;
        return ESP_FAIL;
    }

    if (err != 0) {
        *socket_error = err;
        return ESP_FAIL;
    }

    *socket_error = 0;
    return tcp_set_blocking(fd, true);
}
