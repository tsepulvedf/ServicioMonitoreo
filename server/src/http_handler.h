#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

#include <pthread.h>
#include <time.h>

#include "logger.h"
#include "sensor_manager.h"

typedef struct http_server {
    int listen_fd;
    int running;
    char port[16];
    char web_root[256];
    char auth_host[256];
    char auth_port[16];
    logger_t *logger;
    sensor_manager_t *sensor_manager;
    time_t server_start_time;
    pthread_t thread;
} http_server_t;

int http_server_start(
    http_server_t *server,
    const char *port,
    const char *web_root,
    const char *auth_host,
    const char *auth_port,
    sensor_manager_t *sensor_manager,
    time_t server_start_time,
    logger_t *logger
);
void http_server_stop(http_server_t *server);

#endif
