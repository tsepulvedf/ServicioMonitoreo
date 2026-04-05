#define _POSIX_C_SOURCE 200809L

#include "sensor_manager.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SENSOR_MANAGER_MAX_RECENT 32

typedef struct active_sensor {
    char sensor_id[SENSOR_MANAGER_ID_SIZE];
    char type[SENSOR_MANAGER_TYPE_SIZE];
    double last_value;
    char last_timestamp[SENSOR_MANAGER_TIMESTAMP_SIZE];
    struct active_sensor *next;
} active_sensor_t;

typedef struct operator_session {
    char operator_id[SENSOR_MANAGER_ID_SIZE];
    struct operator_session *next;
} operator_session_t;

typedef struct measurement_entry {
    char sensor_id[SENSOR_MANAGER_ID_SIZE];
    char type[SENSOR_MANAGER_TYPE_SIZE];
    double value;
    char timestamp[SENSOR_MANAGER_TIMESTAMP_SIZE];
    struct measurement_entry *next;
} measurement_entry_t;

typedef struct alert_entry {
    char sensor_id[SENSOR_MANAGER_ID_SIZE];
    char type[SENSOR_MANAGER_TYPE_SIZE];
    double value;
    double threshold;
    char timestamp[SENSOR_MANAGER_TIMESTAMP_SIZE];
    struct alert_entry *next;
} alert_entry_t;

static void free_sensors(active_sensor_t *head) {
    while (head != NULL) {
        active_sensor_t *next = head->next;
        free(head);
        head = next;
    }
}

static void free_operators(operator_session_t *head) {
    while (head != NULL) {
        operator_session_t *next = head->next;
        free(head);
        head = next;
    }
}

static void free_measurements(measurement_entry_t *head) {
    while (head != NULL) {
        measurement_entry_t *next = head->next;
        free(head);
        head = next;
    }
}

static void free_alerts(alert_entry_t *head) {
    while (head != NULL) {
        alert_entry_t *next = head->next;
        free(head);
        head = next;
    }
}

static active_sensor_t *find_sensor(active_sensor_t *head, const char *sensor_id) {
    while (head != NULL) {
        if (strcmp(head->sensor_id, sensor_id) == 0) {
            return head;
        }
        head = head->next;
    }
    return NULL;
}

static int detect_anomaly(const char *type, double value, double *threshold) {
    if (strcmp(type, "temperatura") == 0) {
        if (value < 15.0) {
            *threshold = 15.0;
            return 1;
        }
        if (value > 80.0) {
            *threshold = 80.0;
            return 1;
        }
        return 0;
    }

    if (strcmp(type, "vibracion") == 0) {
        if (value > 40.0) {
            *threshold = 40.0;
            return 1;
        }
        return 0;
    }

    if (strcmp(type, "energia") == 0) {
        if (value > 450.0) {
            *threshold = 450.0;
            return 1;
        }
        return 0;
    }

    return 0;
}

static void trim_measurements(sensor_manager_t *manager) {
    measurement_entry_t *current = manager->measurements;
    measurement_entry_t *previous = NULL;
    size_t count = 0;

    while (current != NULL) {
        count++;
        if (count > SENSOR_MANAGER_MAX_RECENT) {
            previous->next = NULL;
            free_measurements(current);
            return;
        }
        previous = current;
        current = current->next;
    }
}

static void trim_alerts(sensor_manager_t *manager) {
    alert_entry_t *current = manager->alerts;
    alert_entry_t *previous = NULL;
    size_t count = 0;

    while (current != NULL) {
        count++;
        if (count > SENSOR_MANAGER_MAX_RECENT) {
            previous->next = NULL;
            free_alerts(current);
            return;
        }
        previous = current;
        current = current->next;
    }
}

static int append_text(char *buffer, size_t buffer_size, size_t *offset, const char *text) {
    int written;

    if (*offset >= buffer_size) {
        return -1;
    }

    written = snprintf(buffer + *offset, buffer_size - *offset, "%s", text);
    if (written < 0 || (size_t)written >= buffer_size - *offset) {
        return -1;
    }

    *offset += (size_t)written;
    return 0;
}

int sensor_manager_init(sensor_manager_t *manager) {
    if (manager == NULL) {
        return -1;
    }

    memset(manager, 0, sizeof(*manager));
    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        return -1;
    }

    return 0;
}

void sensor_manager_destroy(sensor_manager_t *manager) {
    if (manager == NULL) {
        return;
    }

    pthread_mutex_lock(&manager->mutex);
    free_sensors(manager->sensors);
    free_operators(manager->operators);
    free_measurements(manager->measurements);
    free_alerts(manager->alerts);
    manager->sensors = NULL;
    manager->operators = NULL;
    manager->measurements = NULL;
    manager->alerts = NULL;
    pthread_mutex_unlock(&manager->mutex);
    pthread_mutex_destroy(&manager->mutex);
}

int sensor_manager_register_sensor(sensor_manager_t *manager, const char *sensor_id) {
    active_sensor_t *sensor;

    if (manager == NULL || sensor_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);
    sensor = find_sensor(manager->sensors, sensor_id);
    if (sensor == NULL) {
        sensor = calloc(1, sizeof(*sensor));
        if (sensor == NULL) {
            pthread_mutex_unlock(&manager->mutex);
            return -1;
        }
        snprintf(sensor->sensor_id, sizeof(sensor->sensor_id), "%s", sensor_id);
        sensor->next = manager->sensors;
        manager->sensors = sensor;
    }
    pthread_mutex_unlock(&manager->mutex);
    return 0;
}

int sensor_manager_unregister_sensor(sensor_manager_t *manager, const char *sensor_id) {
    active_sensor_t *current;
    active_sensor_t *previous;

    if (manager == NULL || sensor_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);
    current = manager->sensors;
    previous = NULL;
    while (current != NULL) {
        if (strcmp(current->sensor_id, sensor_id) == 0) {
            if (previous == NULL) {
                manager->sensors = current->next;
            } else {
                previous->next = current->next;
            }
            free(current);
            break;
        }
        previous = current;
        current = current->next;
    }
    pthread_mutex_unlock(&manager->mutex);
    return 0;
}

int sensor_manager_register_operator(sensor_manager_t *manager, const char *operator_id) {
    operator_session_t *current;
    operator_session_t *operator_session;

    if (manager == NULL || operator_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);
    current = manager->operators;
    while (current != NULL) {
        if (strcmp(current->operator_id, operator_id) == 0) {
            pthread_mutex_unlock(&manager->mutex);
            return 0;
        }
        current = current->next;
    }

    operator_session = calloc(1, sizeof(*operator_session));
    if (operator_session == NULL) {
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    snprintf(operator_session->operator_id, sizeof(operator_session->operator_id), "%s", operator_id);
    operator_session->next = manager->operators;
    manager->operators = operator_session;
    pthread_mutex_unlock(&manager->mutex);
    return 0;
}

int sensor_manager_unregister_operator(sensor_manager_t *manager, const char *operator_id) {
    operator_session_t *current;
    operator_session_t *previous;

    if (manager == NULL || operator_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);
    current = manager->operators;
    previous = NULL;
    while (current != NULL) {
        if (strcmp(current->operator_id, operator_id) == 0) {
            if (previous == NULL) {
                manager->operators = current->next;
            } else {
                previous->next = current->next;
            }
            free(current);
            break;
        }
        previous = current;
        current = current->next;
    }
    pthread_mutex_unlock(&manager->mutex);
    return 0;
}

int sensor_manager_record_measurement(
    sensor_manager_t *manager,
    const char *sensor_id,
    const char *type,
    double value,
    const char *timestamp,
    int *is_anomaly,
    double *threshold
) {
    active_sensor_t *sensor;
    measurement_entry_t *measurement;

    if (manager == NULL || sensor_id == NULL || type == NULL || timestamp == NULL ||
        is_anomaly == NULL || threshold == NULL) {
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);
    sensor = find_sensor(manager->sensors, sensor_id);
    if (sensor == NULL) {
        sensor = calloc(1, sizeof(*sensor));
        if (sensor == NULL) {
            pthread_mutex_unlock(&manager->mutex);
            return -1;
        }
        snprintf(sensor->sensor_id, sizeof(sensor->sensor_id), "%s", sensor_id);
        sensor->next = manager->sensors;
        manager->sensors = sensor;
    }

    snprintf(sensor->type, sizeof(sensor->type), "%s", type);
    sensor->last_value = value;
    snprintf(sensor->last_timestamp, sizeof(sensor->last_timestamp), "%s", timestamp);

    measurement = calloc(1, sizeof(*measurement));
    if (measurement == NULL) {
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    snprintf(measurement->sensor_id, sizeof(measurement->sensor_id), "%s", sensor_id);
    snprintf(measurement->type, sizeof(measurement->type), "%s", type);
    measurement->value = value;
    snprintf(measurement->timestamp, sizeof(measurement->timestamp), "%s", timestamp);
    measurement->next = manager->measurements;
    manager->measurements = measurement;
    trim_measurements(manager);

    *threshold = 0.0;
    *is_anomaly = detect_anomaly(type, value, threshold);
    if (*is_anomaly) {
        alert_entry_t *alert = calloc(1, sizeof(*alert));
        if (alert == NULL) {
            pthread_mutex_unlock(&manager->mutex);
            return -1;
        }
        snprintf(alert->sensor_id, sizeof(alert->sensor_id), "%s", sensor_id);
        snprintf(alert->type, sizeof(alert->type), "%s", type);
        alert->value = value;
        alert->threshold = *threshold;
        snprintf(alert->timestamp, sizeof(alert->timestamp), "%s", timestamp);
        alert->next = manager->alerts;
        manager->alerts = alert;
        trim_alerts(manager);
    }

    pthread_mutex_unlock(&manager->mutex);
    return 0;
}

int sensor_manager_get_status(
    sensor_manager_t *manager,
    size_t *active_sensors,
    size_t *active_operators
) {
    active_sensor_t *sensor;
    operator_session_t *operator_session;

    if (manager == NULL || active_sensors == NULL || active_operators == NULL) {
        return -1;
    }

    *active_sensors = 0;
    *active_operators = 0;

    pthread_mutex_lock(&manager->mutex);
    for (sensor = manager->sensors; sensor != NULL; sensor = sensor->next) {
        (*active_sensors)++;
    }
    for (operator_session = manager->operators; operator_session != NULL; operator_session = operator_session->next) {
        (*active_operators)++;
    }
    pthread_mutex_unlock(&manager->mutex);

    return 0;
}

int sensor_manager_build_query_result(
    sensor_manager_t *manager,
    const char *target,
    char *buffer,
    size_t buffer_size
) {
    size_t offset = 0;
    int first = 1;

    if (manager == NULL || target == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);
    if (append_text(buffer, buffer_size, &offset, "[") != 0) {
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    if (strcmp(target, "SENSORS") == 0) {
        active_sensor_t *current = manager->sensors;
        while (current != NULL) {
            char item[512];
            snprintf(
                item,
                sizeof(item),
                "%s{\"sensor_id\":\"%s\",\"tipo\":\"%s\",\"ultimo_valor\":%.2f,\"ultimo_timestamp\":\"%s\",\"estado\":\"online\"}",
                first ? "" : ",",
                current->sensor_id,
                current->type[0] != '\0' ? current->type : "desconocido",
                current->last_value,
                current->last_timestamp
            );
            if (append_text(buffer, buffer_size, &offset, item) != 0) {
                pthread_mutex_unlock(&manager->mutex);
                return -1;
            }
            first = 0;
            current = current->next;
        }
    } else if (strcmp(target, "MEASUREMENTS") == 0) {
        measurement_entry_t *current = manager->measurements;
        while (current != NULL) {
            char item[512];
            snprintf(
                item,
                sizeof(item),
                "%s{\"sensor_id\":\"%s\",\"tipo\":\"%s\",\"valor\":%.2f,\"timestamp\":\"%s\"}",
                first ? "" : ",",
                current->sensor_id,
                current->type,
                current->value,
                current->timestamp
            );
            if (append_text(buffer, buffer_size, &offset, item) != 0) {
                pthread_mutex_unlock(&manager->mutex);
                return -1;
            }
            first = 0;
            current = current->next;
        }
    } else if (strcmp(target, "ALERTS") == 0) {
        alert_entry_t *current = manager->alerts;
        while (current != NULL) {
            char item[512];
            snprintf(
                item,
                sizeof(item),
                "%s{\"sensor_id\":\"%s\",\"tipo\":\"%s\",\"valor\":%.2f,\"umbral\":%.2f,\"timestamp\":\"%s\"}",
                first ? "" : ",",
                current->sensor_id,
                current->type,
                current->value,
                current->threshold,
                current->timestamp
            );
            if (append_text(buffer, buffer_size, &offset, item) != 0) {
                pthread_mutex_unlock(&manager->mutex);
                return -1;
            }
            first = 0;
            current = current->next;
        }
    } else {
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    if (append_text(buffer, buffer_size, &offset, "]") != 0) {
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    pthread_mutex_unlock(&manager->mutex);
    return 0;
}
