#ifndef LOGGER_H
#define LOGGER_H

#include <pthread.h>
#include <stdio.h>

typedef struct logger {
    FILE *file;
    pthread_mutex_t mutex;
} logger_t;

int logger_init(logger_t *logger, const char *path);
void logger_close(logger_t *logger);
void logger_log_message(logger_t *logger, const char *origin, const char *message);
void logger_log_transaction(
    logger_t *logger,
    const char *ip,
    const char *port,
    const char *request,
    const char *response
);

#endif
