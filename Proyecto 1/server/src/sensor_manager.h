#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <pthread.h>
#include <stddef.h>

#define SENSOR_MANAGER_ID_SIZE 64
#define SENSOR_MANAGER_TYPE_SIZE 32
#define SENSOR_MANAGER_TIMESTAMP_SIZE 64

typedef struct sensor_manager {
    pthread_mutex_t mutex;
    void *sensors;
    void *operators;
    void *measurements;
    void *alerts;
} sensor_manager_t;

int sensor_manager_init(sensor_manager_t *manager);
void sensor_manager_destroy(sensor_manager_t *manager);

int sensor_manager_register_sensor(sensor_manager_t *manager, const char *sensor_id);
int sensor_manager_unregister_sensor(sensor_manager_t *manager, const char *sensor_id);
int sensor_manager_register_operator(sensor_manager_t *manager, const char *operator_id);
int sensor_manager_unregister_operator(sensor_manager_t *manager, const char *operator_id);

int sensor_manager_record_measurement(
    sensor_manager_t *manager,
    const char *sensor_id,
    const char *type,
    double value,
    const char *timestamp,
    int *is_anomaly,
    double *threshold
);

int sensor_manager_get_status(
    sensor_manager_t *manager,
    size_t *active_sensors,
    size_t *active_operators
);

int sensor_manager_build_query_result(
    sensor_manager_t *manager,
    const char *target,
    char *buffer,
    size_t buffer_size
);

#endif
