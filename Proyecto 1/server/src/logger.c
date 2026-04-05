#define _POSIX_C_SOURCE 200809L

#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void format_timestamp(char *buffer, size_t size) {
    time_t now;
    struct tm tm_utc;

    now = time(NULL);
    gmtime_r(&now, &tm_utc);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

static void logger_write(logger_t *logger, const char *line) {
    if (logger == NULL || line == NULL) {
        return;
    }

    pthread_mutex_lock(&logger->mutex);
    fputs(line, stdout);
    fflush(stdout);

    if (logger->file != NULL) {
        fputs(line, logger->file);
        fflush(logger->file);
    }
    pthread_mutex_unlock(&logger->mutex);
}

int logger_init(logger_t *logger, const char *path) {
    if (logger == NULL || path == NULL) {
        return -1;
    }

    memset(logger, 0, sizeof(*logger));
    if (pthread_mutex_init(&logger->mutex, NULL) != 0) {
        return -1;
    }

    logger->file = fopen(path, "a");
    if (logger->file == NULL) {
        pthread_mutex_destroy(&logger->mutex);
        return -1;
    }

    return 0;
}

void logger_close(logger_t *logger) {
    if (logger == NULL) {
        return;
    }

    pthread_mutex_lock(&logger->mutex);
    if (logger->file != NULL) {
        fclose(logger->file);
        logger->file = NULL;
    }
    pthread_mutex_unlock(&logger->mutex);
    pthread_mutex_destroy(&logger->mutex);
}

void logger_log_message(logger_t *logger, const char *origin, const char *message) {
    char timestamp[32];
    char line[2048];

    format_timestamp(timestamp, sizeof(timestamp));
    snprintf(
        line,
        sizeof(line),
        "[%s] [%s] %s\n",
        timestamp,
        origin != NULL ? origin : "server",
        message != NULL ? message : ""
    );
    logger_write(logger, line);
}

void logger_log_transaction(
    logger_t *logger,
    const char *ip,
    const char *port,
    const char *request,
    const char *response
) {
    char timestamp[32];
    char line[4096];

    format_timestamp(timestamp, sizeof(timestamp));
    snprintf(
        line,
        sizeof(line),
        "[%s] [%s:%s] REQ: %s RSP: %s\n",
        timestamp,
        ip != NULL ? ip : "-",
        port != NULL ? port : "-",
        request != NULL ? request : "",
        response != NULL ? response : ""
    );
    logger_write(logger, line);
}
