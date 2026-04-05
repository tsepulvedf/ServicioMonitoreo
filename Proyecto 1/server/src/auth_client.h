#ifndef AUTH_CLIENT_H
#define AUTH_CLIENT_H

#include <stddef.h>

typedef enum auth_result {
    AUTH_RESULT_OK = 0,
    AUTH_RESULT_DENIED = 1,
    AUTH_RESULT_UNAVAILABLE = 2,
    AUTH_RESULT_PROTOCOL_ERROR = 3
} auth_result_t;

auth_result_t auth_client_authenticate(
    const char *hostname,
    const char *port,
    const char *username,
    const char *password,
    char *role_buffer,
    size_t role_buffer_size,
    char *reason_buffer,
    size_t reason_buffer_size
);

#endif
