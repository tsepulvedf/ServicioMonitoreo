#define _POSIX_C_SOURCE 200809L

#include "auth_client.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int connect_to_service(const char *hostname, const char *port) {
    struct addrinfo hints;
    struct addrinfo *results;
    struct addrinfo *current;
    int sockfd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, port, &hints, &results) != 0) {
        return -1;
    }

    sockfd = -1;
    for (current = results; current != NULL; current = current->ai_next) {
        sockfd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (sockfd < 0) {
            continue;
        }

        if (connect(sockfd, current->ai_addr, current->ai_addrlen) == 0) {
            break;
        }

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(results);
    return sockfd;
}

static int send_all(int fd, const char *buffer, size_t length) {
    size_t sent = 0;

    while (sent < length) {
        ssize_t rc = send(fd, buffer + sent, length - sent, 0);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rc == 0) {
            return -1;
        }
        sent += (size_t)rc;
    }

    return 0;
}

static int recv_line(int fd, char *buffer, size_t size) {
    size_t offset = 0;

    if (size == 0) {
        return -1;
    }

    while (offset + 1 < size) {
        char ch;
        ssize_t rc = recv(fd, &ch, 1, 0);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rc == 0) {
            break;
        }

        buffer[offset++] = ch;
        if (offset >= 2 && buffer[offset - 2] == '\r' && buffer[offset - 1] == '\n') {
            buffer[offset - 2] = '\0';
            return (int)(offset - 2);
        }
    }

    buffer[offset] = '\0';
    return offset > 0 ? (int)offset : -1;
}

auth_result_t auth_client_authenticate(
    const char *hostname,
    const char *port,
    const char *username,
    const char *password,
    char *role_buffer,
    size_t role_buffer_size,
    char *reason_buffer,
    size_t reason_buffer_size
) {
    char request[512];
    char response[256];
    int sockfd;

    if (hostname == NULL || port == NULL || username == NULL || password == NULL ||
        role_buffer == NULL || reason_buffer == NULL) {
        return AUTH_RESULT_PROTOCOL_ERROR;
    }

    role_buffer[0] = '\0';
    reason_buffer[0] = '\0';

    sockfd = connect_to_service(hostname, port);
    if (sockfd < 0) {
        snprintf(reason_buffer, reason_buffer_size, "auth service unavailable");
        return AUTH_RESULT_UNAVAILABLE;
    }

    snprintf(request, sizeof(request), "AUTH|%s|%s\r\n", username, password);
    if (send_all(sockfd, request, strlen(request)) != 0) {
        close(sockfd);
        snprintf(reason_buffer, reason_buffer_size, "auth send failed");
        return AUTH_RESULT_UNAVAILABLE;
    }

    if (recv_line(sockfd, response, sizeof(response)) < 0) {
        close(sockfd);
        snprintf(reason_buffer, reason_buffer_size, "auth recv failed");
        return AUTH_RESULT_UNAVAILABLE;
    }

    close(sockfd);

    if (strncmp(response, "OK|", 3) == 0) {
        snprintf(role_buffer, role_buffer_size, "%s", response + 3);
        return AUTH_RESULT_OK;
    }

    if (strncmp(response, "FAIL|", 5) == 0) {
        snprintf(reason_buffer, reason_buffer_size, "%s", response + 5);
        return AUTH_RESULT_DENIED;
    }

    snprintf(reason_buffer, reason_buffer_size, "invalid auth response");
    return AUTH_RESULT_PROTOCOL_ERROR;
}
