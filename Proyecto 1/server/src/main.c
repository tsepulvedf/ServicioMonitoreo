#define _POSIX_C_SOURCE 200809L

#include "http_handler.h"
#include "logger.h"
#include "protocol.h"
#include "sensor_manager.h"

#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct client_thread_args {
    int client_fd;
    logger_t *logger;
    protocol_context_t *protocol_context;
    client_session_t session;
} client_thread_args_t;

static int create_server_socket(const char *port) {
    struct addrinfo hints;
    struct addrinfo *results;
    struct addrinfo *current;
    int listen_fd;
    int opt = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &hints, &results) != 0) {
        return -1;
    }

    listen_fd = -1;
    for (current = results; current != NULL; current = current->ai_next) {
        listen_fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (listen_fd < 0) {
            continue;
        }

        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (bind(listen_fd, current->ai_addr, current->ai_addrlen) == 0 &&
            listen(listen_fd, 16) == 0) {
            break;
        }

        close(listen_fd);
        listen_fd = -1;
    }

    freeaddrinfo(results);
    return listen_fd;
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

static int recv_iotp_line(int fd, char *buffer, size_t size) {
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
            if (offset == 0) {
                return 0;
            }
            buffer[offset] = '\0';
            return -1;
        }

        buffer[offset++] = ch;
        if (offset >= 2 && buffer[offset - 2] == '\r' && buffer[offset - 1] == '\n') {
            buffer[offset] = '\0';
            return (int)offset;
        }
    }

    buffer[size - 1] = '\0';
    return -2;
}

static void cleanup_session(protocol_context_t *context, client_session_t *session) {
    if (context == NULL || session == NULL || !session->authenticated) {
        return;
    }

    if (session->role == CLIENT_ROLE_SENSOR) {
        sensor_manager_unregister_sensor(context->sensor_manager, session->client_id);
    } else if (session->role == CLIENT_ROLE_OPERATOR) {
        sensor_manager_unregister_operator(context->sensor_manager, session->client_id);
    }

    session->authenticated = false;
    session->role = CLIENT_ROLE_NONE;
    session->client_id[0] = '\0';
}

static void *client_thread_main(void *arg) {
    client_thread_args_t *args = arg;
    char request[IOTP_MAX_MESSAGE_LEN];
    char response[IOTP_MAX_QUERY_PAYLOAD + 64];
    char sanitized_request[IOTP_MAX_MESSAGE_LEN];

    pthread_detach(pthread_self());

    while (true) {
        int rc;
        protocol_result_t result;

        rc = recv_iotp_line(args->client_fd, request, sizeof(request));
        if (rc == 0) {
            break;
        }
        if (rc == -2) {
            protocol_build_error("MALFORMED_MESSAGE", "message too long", response, sizeof(response));
            snprintf(sanitized_request, sizeof(sanitized_request), "message too long");
            logger_log_transaction(
                args->logger,
                args->session.peer_ip,
                args->session.peer_port,
                sanitized_request,
                "ERROR|MALFORMED_MESSAGE|message too long"
            );
            send_all(args->client_fd, response, strlen(response));
            break;
        }
        if (rc < 0) {
            logger_log_message(args->logger, "iotp", "recv failed");
            break;
        }

        protocol_sanitize_request_for_log(request, sanitized_request, sizeof(sanitized_request));
        if (protocol_handle_request(
                args->protocol_context,
                &args->session,
                request,
                response,
                sizeof(response),
                &result
            ) != 0) {
            protocol_build_error("INTERNAL_ERROR", "request handling failed", response, sizeof(response));
        }

        logger_log_transaction(args->logger, args->session.peer_ip, args->session.peer_port, sanitized_request, response);
        if (send_all(args->client_fd, response, strlen(response)) != 0) {
            logger_log_message(args->logger, "iotp", "send failed");
            break;
        }

        if (result.generated_alert) {
            logger_log_message(args->logger, "alert", result.alert_message);
        }
        if (result.close_connection) {
            break;
        }
    }

    cleanup_session(args->protocol_context, &args->session);
    close(args->client_fd);
    free(args);
    return NULL;
}

static const char *env_or_default(const char *name, const char *fallback) {
    const char *value = getenv(name);

    if (value == NULL || value[0] == '\0') {
        return fallback;
    }
    return value;
}

int main(int argc, char *argv[]) {
    const char *port;
    const char *log_path;
    const char *auth_host;
    const char *auth_port;
    const char *http_port;
    const char *web_root;
    int listen_fd;
    logger_t logger;
    sensor_manager_t sensor_manager;
    protocol_context_t protocol_context;
    http_server_t http_server;

    if (argc != 3) {
        fprintf(stderr, "Uso: %s <puerto> <archivoDeLogs>\n", argv[0]);
        return 1;
    }

    port = argv[1];
    log_path = argv[2];
    auth_host = env_or_default("AUTH_HOST", "auth-service");
    auth_port = env_or_default("AUTH_PORT", "9001");
    http_port = env_or_default("HTTP_PORT", "8080");
    web_root = env_or_default("HTTP_WEB_ROOT", "web");

    if (logger_init(&logger, log_path) != 0) {
        fprintf(stderr, "No se pudo abrir el archivo de logs: %s\n", log_path);
        return 1;
    }

    if (sensor_manager_init(&sensor_manager) != 0) {
        logger_log_message(&logger, "server", "sensor_manager_init failed");
        logger_close(&logger);
        return 1;
    }

    memset(&protocol_context, 0, sizeof(protocol_context));
    protocol_context.auth_host = auth_host;
    protocol_context.auth_port = auth_port;
    protocol_context.sensor_manager = &sensor_manager;
    protocol_context.server_start_time = time(NULL);

    memset(&http_server, 0, sizeof(http_server));
    http_server.listen_fd = -1;
    if (http_server_start(
            &http_server,
            http_port,
            web_root,
            auth_host,
            auth_port,
            &sensor_manager,
            protocol_context.server_start_time,
            &logger
        ) != 0) {
        logger_log_message(&logger, "http", "http server start failed; continuing without HTTP");
    } else {
        logger_log_message(&logger, "http", "http server started");
    }

    listen_fd = create_server_socket(port);
    if (listen_fd < 0) {
        logger_log_message(&logger, "server", "failed to create IOTP listener");
        http_server_stop(&http_server);
        sensor_manager_destroy(&sensor_manager);
        logger_close(&logger);
        return 1;
    }

    logger_log_message(&logger, "server", "IOTP server started");

    while (true) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        char host[NI_MAXHOST];
        char service[NI_MAXSERV];
        int client_fd;
        client_thread_args_t *thread_args;
        pthread_t thread;
        struct timeval timeout;

        client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            logger_log_message(&logger, "server", "accept failed");
            continue;
        }

        if (getnameinfo(
                (struct sockaddr *)&client_addr,
                client_len,
                host,
                sizeof(host),
                service,
                sizeof(service),
                NI_NUMERICHOST | NI_NUMERICSERV
            ) != 0) {
            snprintf(host, sizeof(host), "unknown");
            snprintf(service, sizeof(service), "0");
        }

        timeout.tv_sec = 30;
        timeout.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        thread_args = calloc(1, sizeof(*thread_args));
        if (thread_args == NULL) {
            logger_log_message(&logger, "server", "client thread allocation failed");
            close(client_fd);
            continue;
        }

        thread_args->client_fd = client_fd;
        thread_args->logger = &logger;
        thread_args->protocol_context = &protocol_context;
        snprintf(thread_args->session.peer_ip, sizeof(thread_args->session.peer_ip), "%s", host);
        snprintf(thread_args->session.peer_port, sizeof(thread_args->session.peer_port), "%s", service);

        if (pthread_create(&thread, NULL, client_thread_main, thread_args) != 0) {
            logger_log_message(&logger, "server", "pthread_create failed");
            close(client_fd);
            free(thread_args);
            continue;
        }
    }

    close(listen_fd);
    http_server_stop(&http_server);
    sensor_manager_destroy(&sensor_manager);
    logger_close(&logger);
    return 0;
}
