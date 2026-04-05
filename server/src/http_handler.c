#define _POSIX_C_SOURCE 200809L

#include "http_handler.h"

#include "auth_client.h"

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define HTTP_REQUEST_LINE_SIZE 2048
#define HTTP_PATH_SIZE 512
#define HTTP_RESPONSE_SIZE 4096

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

static int recv_request_line(int client_fd, char *buffer, size_t size) {
    size_t offset = 0;

    if (size == 0) {
        return -1;
    }

    while (offset + 1 < size) {
        char ch;
        ssize_t rc = recv(client_fd, &ch, 1, 0);
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

static const char *content_type_for_path(const char *path) {
    const char *extension = strrchr(path, '.');

    if (extension == NULL) {
        return "application/octet-stream";
    }
    if (strcmp(extension, ".html") == 0) {
        return "text/html; charset=US-ASCII";
    }
    if (strcmp(extension, ".css") == 0) {
        return "text/css; charset=US-ASCII";
    }
    if (strcmp(extension, ".js") == 0) {
        return "application/javascript; charset=US-ASCII";
    }
    if (strcmp(extension, ".json") == 0) {
        return "application/json; charset=US-ASCII";
    }
    return "application/octet-stream";
}

static void sanitize_http_request_line(const char *request_line, char *buffer, size_t buffer_size) {
    const char *pass_key;
    size_t prefix_length;

    if (request_line == NULL || buffer == NULL || buffer_size == 0) {
        return;
    }

    pass_key = strstr(request_line, "pass=");
    if (pass_key == NULL) {
        snprintf(buffer, buffer_size, "%s", request_line);
        return;
    }

    prefix_length = (size_t)(pass_key - request_line);
    if (prefix_length >= buffer_size) {
        prefix_length = buffer_size - 1;
    }

    memcpy(buffer, request_line, prefix_length);
    buffer[prefix_length] = '\0';
    strncat(buffer, "pass=<redacted>", buffer_size - strlen(buffer) - 1);

    pass_key = strchr(pass_key, '&');
    if (pass_key == NULL) {
        pass_key = strchr(request_line + prefix_length, ' ');
    }
    if (pass_key != NULL) {
        strncat(buffer, pass_key, buffer_size - strlen(buffer) - 1);
    }
}

static int send_response(
    int client_fd,
    int status_code,
    const char *reason,
    const char *content_type,
    const char *body,
    const char *extra_headers
) {
    char header[HTTP_RESPONSE_SIZE];
    size_t body_length = body != NULL ? strlen(body) : 0;

    snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        status_code,
        reason,
        content_type != NULL ? content_type : "text/plain; charset=US-ASCII",
        body_length,
        extra_headers != NULL ? extra_headers : ""
    );

    if (send_all(client_fd, header, strlen(header)) != 0) {
        return -1;
    }
    if (body_length > 0 && send_all(client_fd, body, body_length) != 0) {
        return -1;
    }

    return 0;
}

static void send_simple_response(int client_fd, int status_code, const char *reason, const char *body) {
    send_response(client_fd, status_code, reason, "text/plain; charset=US-ASCII", body, NULL);
}

static void send_redirect(int client_fd, const char *location) {
    char headers[256];

    snprintf(headers, sizeof(headers), "Location: %s\r\n", location);
    send_response(
        client_fd,
        302,
        "Found",
        "text/plain; charset=US-ASCII",
        "302 Found\n",
        headers
    );
}

static int serve_file(int client_fd, const char *file_path) {
    FILE *file;
    long file_size;
    char *content;
    int rc;

    file = fopen(file_path, "rb");
    if (file == NULL) {
        send_simple_response(client_fd, 404, "Not Found", "404 Not Found\n");
        return 404;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        send_simple_response(client_fd, 500, "Internal Server Error", "500 Internal Server Error\n");
        return 500;
    }

    file_size = ftell(file);
    if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        send_simple_response(client_fd, 500, "Internal Server Error", "500 Internal Server Error\n");
        return 500;
    }

    content = malloc((size_t)file_size + 1);
    if (content == NULL) {
        fclose(file);
        send_simple_response(client_fd, 500, "Internal Server Error", "500 Internal Server Error\n");
        return 500;
    }

    if (file_size > 0 && fread(content, 1, (size_t)file_size, file) != (size_t)file_size) {
        free(content);
        fclose(file);
        send_simple_response(client_fd, 500, "Internal Server Error", "500 Internal Server Error\n");
        return 500;
    }
    content[file_size] = '\0';

    fclose(file);
    rc = send_response(client_fd, 200, "OK", content_type_for_path(file_path), content, NULL);
    free(content);
    return rc == 0 ? 200 : 500;
}

static int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    return -1;
}

static void url_decode(const char *input, char *output, size_t output_size) {
    size_t in_index = 0;
    size_t out_index = 0;

    if (output_size == 0) {
        return;
    }

    while (input[in_index] != '\0' && out_index + 1 < output_size) {
        if (input[in_index] == '+' ) {
            output[out_index++] = ' ';
            in_index++;
            continue;
        }

        if (input[in_index] == '%' &&
            input[in_index + 1] != '\0' &&
            input[in_index + 2] != '\0') {
            int high = hex_value(input[in_index + 1]);
            int low = hex_value(input[in_index + 2]);

            if (high >= 0 && low >= 0) {
                output[out_index++] = (char)((high << 4) | low);
                in_index += 3;
                continue;
            }
        }

        output[out_index++] = input[in_index++];
    }

    output[out_index] = '\0';
}

static int get_query_param(
    const char *query,
    const char *key,
    char *buffer,
    size_t buffer_size
) {
    const char *current;
    size_t key_length;

    if (query == NULL || key == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }

    key_length = strlen(key);
    current = query;
    while (*current != '\0') {
        const char *next = strchr(current, '&');
        const char *equals = strchr(current, '=');
        size_t segment_length;

        if (next == NULL) {
            next = current + strlen(current);
        }
        segment_length = (size_t)(next - current);

        if (equals != NULL && equals < next && (size_t)(equals - current) == key_length &&
            strncmp(current, key, key_length) == 0) {
            char encoded[256];
            size_t value_length = (size_t)(next - equals - 1);

            if (value_length >= sizeof(encoded)) {
                value_length = sizeof(encoded) - 1;
            }
            memcpy(encoded, equals + 1, value_length);
            encoded[value_length] = '\0';
            url_decode(encoded, buffer, buffer_size);
            return 0;
        }

        if (*next == '\0') {
            break;
        }
        current = next + 1;
        if (segment_length == 0) {
            break;
        }
    }

    buffer[0] = '\0';
    return -1;
}

static int build_status_json(http_server_t *server, char *buffer, size_t buffer_size) {
    size_t active_sensors;
    size_t active_operators;
    time_t now;

    if (sensor_manager_get_status(server->sensor_manager, &active_sensors, &active_operators) != 0) {
        return -1;
    }

    now = time(NULL);
    return snprintf(
        buffer,
        buffer_size,
        "{\"active_sensors\":%zu,\"active_operators\":%zu,\"uptime_seconds\":%lu}",
        active_sensors,
        active_operators,
        (unsigned long)(now - server->server_start_time)
    ) >= (int)buffer_size ? -1 : 0;
}

static int handle_login(http_server_t *server, int client_fd, const char *query) {
    char user[128];
    char pass[128];
    char role[32];
    char reason[128];
    auth_result_t auth_result;

    if (get_query_param(query, "user", user, sizeof(user)) != 0 ||
        get_query_param(query, "pass", pass, sizeof(pass)) != 0 ||
        user[0] == '\0' || pass[0] == '\0') {
        send_simple_response(client_fd, 400, "Bad Request", "400 Bad Request\n");
        return 400;
    }

    auth_result = auth_client_authenticate(
        server->auth_host,
        server->auth_port,
        user,
        pass,
        role,
        sizeof(role),
        reason,
        sizeof(reason)
    );

    if (auth_result == AUTH_RESULT_OK) {
        if (strcmp(role, "operator") != 0) {
            send_simple_response(client_fd, 403, "Forbidden", "403 Forbidden\n");
            return 403;
        }
        send_redirect(client_fd, "/dashboard");
        return 302;
    }

    if (auth_result == AUTH_RESULT_DENIED) {
        send_simple_response(client_fd, 401, "Unauthorized", "401 Unauthorized\n");
        return 401;
    }

    send_simple_response(client_fd, 503, "Service Unavailable", "503 Service Unavailable\n");
    return 503;
}

static void log_http_transaction(
    http_server_t *server,
    const char *ip,
    const char *port,
    const char *request_line,
    const char *response_summary
) {
    char sanitized_request[HTTP_REQUEST_LINE_SIZE];

    sanitize_http_request_line(request_line, sanitized_request, sizeof(sanitized_request));
    logger_log_transaction(server->logger, ip, port, sanitized_request, response_summary);
}

static void handle_http_client(http_server_t *server, int client_fd, const char *ip, const char *port) {
    char request_line[HTTP_REQUEST_LINE_SIZE];
    char method[16];
    char target[HTTP_PATH_SIZE];
    char version[32];
    char path[HTTP_PATH_SIZE];
    char *query;
    char file_path[512];

    if (recv_request_line(client_fd, request_line, sizeof(request_line)) < 0) {
        return;
    }

    if (sscanf(request_line, "%15s %511s %31s", method, target, version) != 3) {
        send_simple_response(client_fd, 400, "Bad Request", "400 Bad Request\n");
        log_http_transaction(server, ip, port, request_line, "HTTP/1.1 400 Bad Request");
        return;
    }

    if (strcmp(method, "GET") != 0) {
        send_simple_response(client_fd, 405, "Method Not Allowed", "405 Method Not Allowed\n");
        log_http_transaction(server, ip, port, request_line, "HTTP/1.1 405 Method Not Allowed");
        return;
    }

    snprintf(path, sizeof(path), "%s", target);
    query = strchr(path, '?');
    if (query != NULL) {
        *query = '\0';
        query++;
    }

    if (strstr(path, "..") != NULL) {
        send_simple_response(client_fd, 404, "Not Found", "404 Not Found\n");
        log_http_transaction(server, ip, port, request_line, "HTTP/1.1 404 Not Found");
        return;
    }

    if (strcmp(path, "/") == 0) {
        int status_code;

        snprintf(file_path, sizeof(file_path), "%s/login.html", server->web_root);
        status_code = serve_file(client_fd, file_path);
        if (status_code == 200) {
            log_http_transaction(server, ip, port, request_line, "HTTP/1.1 200 OK");
        } else if (status_code == 404) {
            log_http_transaction(server, ip, port, request_line, "HTTP/1.1 404 Not Found");
        } else {
            log_http_transaction(server, ip, port, request_line, "HTTP/1.1 500 Internal Server Error");
        }
        return;
    }

    if (strcmp(path, "/dashboard") == 0) {
        int status_code;

        snprintf(file_path, sizeof(file_path), "%s/dashboard.html", server->web_root);
        status_code = serve_file(client_fd, file_path);
        if (status_code == 200) {
            log_http_transaction(server, ip, port, request_line, "HTTP/1.1 200 OK");
        } else if (status_code == 404) {
            log_http_transaction(server, ip, port, request_line, "HTTP/1.1 404 Not Found");
        } else {
            log_http_transaction(server, ip, port, request_line, "HTTP/1.1 500 Internal Server Error");
        }
        return;
    }

    if (strcmp(path, "/login") == 0) {
        int status_code = handle_login(server, client_fd, query);

        switch (status_code) {
            case 302:
                log_http_transaction(server, ip, port, request_line, "HTTP/1.1 302 Found");
                break;
            case 400:
                log_http_transaction(server, ip, port, request_line, "HTTP/1.1 400 Bad Request");
                break;
            case 401:
                log_http_transaction(server, ip, port, request_line, "HTTP/1.1 401 Unauthorized");
                break;
            case 403:
                log_http_transaction(server, ip, port, request_line, "HTTP/1.1 403 Forbidden");
                break;
            default:
                log_http_transaction(server, ip, port, request_line, "HTTP/1.1 503 Service Unavailable");
                break;
        }
        return;
    }

    if (strcmp(path, "/status") == 0) {
        char body[256];

        if (build_status_json(server, body, sizeof(body)) != 0) {
            send_simple_response(client_fd, 500, "Internal Server Error", "500 Internal Server Error\n");
            log_http_transaction(server, ip, port, request_line, "HTTP/1.1 500 Internal Server Error");
            return;
        }

        send_response(client_fd, 200, "OK", "application/json; charset=US-ASCII", body, NULL);
        log_http_transaction(server, ip, port, request_line, "HTTP/1.1 200 OK");
        return;
    }

    send_simple_response(client_fd, 404, "Not Found", "404 Not Found\n");
    log_http_transaction(server, ip, port, request_line, "HTTP/1.1 404 Not Found");
}

static int create_http_listener(const char *port) {
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

static void *http_thread_main(void *arg) {
    http_server_t *server = arg;

    while (server->running) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        char host[NI_MAXHOST];
        char service[NI_MAXSERV];
        int client_fd;

        client_fd = accept(server->listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (server->running) {
                logger_log_message(server->logger, "http", "accept failed");
            }
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

        handle_http_client(server, client_fd, host, service);
        close(client_fd);
    }

    return NULL;
}

int http_server_start(
    http_server_t *server,
    const char *port,
    const char *web_root,
    const char *auth_host,
    const char *auth_port,
    sensor_manager_t *sensor_manager,
    time_t server_start_time,
    logger_t *logger
) {
    if (server == NULL || port == NULL || web_root == NULL || auth_host == NULL ||
        auth_port == NULL || sensor_manager == NULL) {
        return -1;
    }

    memset(server, 0, sizeof(*server));
    snprintf(server->port, sizeof(server->port), "%s", port);
    snprintf(server->web_root, sizeof(server->web_root), "%s", web_root);
    snprintf(server->auth_host, sizeof(server->auth_host), "%s", auth_host);
    snprintf(server->auth_port, sizeof(server->auth_port), "%s", auth_port);
    server->logger = logger;
    server->sensor_manager = sensor_manager;
    server->server_start_time = server_start_time;
    server->listen_fd = create_http_listener(server->port);
    if (server->listen_fd < 0) {
        return -1;
    }

    server->running = 1;
    if (pthread_create(&server->thread, NULL, http_thread_main, server) != 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
        server->running = 0;
        return -1;
    }

    pthread_detach(server->thread);
    return 0;
}

void http_server_stop(http_server_t *server) {
    if (server == NULL) {
        return;
    }

    server->running = 0;
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }
}
