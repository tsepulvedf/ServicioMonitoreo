#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "sensor_manager.h"

#define IOTP_MAX_MESSAGE_LEN 1024
#define IOTP_MAX_OPCODE_LEN 16
#define IOTP_MAX_ARGS 6
#define IOTP_MAX_QUERY_PAYLOAD 4096

typedef enum iotp_opcode {
    IOTP_OPCODE_REGISTER = 0,
    IOTP_OPCODE_ACK,
    IOTP_OPCODE_ERROR,
    IOTP_OPCODE_DATA,
    IOTP_OPCODE_ALERT,
    IOTP_OPCODE_QUERY,
    IOTP_OPCODE_RESULT,
    IOTP_OPCODE_STATUS,
    IOTP_OPCODE_STATUSR,
    IOTP_OPCODE_DISCONNECT,
    IOTP_OPCODE_UNKNOWN
} iotp_opcode_t;

typedef enum client_role {
    CLIENT_ROLE_NONE = 0,
    CLIENT_ROLE_SENSOR,
    CLIENT_ROLE_OPERATOR
} client_role_t;

typedef struct client_session {
    bool authenticated;
    client_role_t role;
    char client_id[SENSOR_MANAGER_ID_SIZE];
    char peer_ip[64];
    char peer_port[32];
} client_session_t;

typedef struct protocol_message {
    iotp_opcode_t opcode;
    char opcode_text[IOTP_MAX_OPCODE_LEN];
    int arg_count;
    char storage[IOTP_MAX_MESSAGE_LEN];
    char *args[IOTP_MAX_ARGS];
} protocol_message_t;

typedef struct protocol_context {
    const char *auth_host;
    const char *auth_port;
    sensor_manager_t *sensor_manager;
    time_t server_start_time;
} protocol_context_t;

typedef struct protocol_result {
    bool close_connection;
    bool generated_alert;
    char alert_message[IOTP_MAX_MESSAGE_LEN];
} protocol_result_t;

const char *protocol_opcode_name(iotp_opcode_t opcode);
int protocol_parse_message(const char *raw_message, protocol_message_t *message);
int protocol_validate_opcode(const char *opcode_text, iotp_opcode_t *opcode);
int protocol_build_ack(const char *message, char *buffer, size_t buffer_size);
int protocol_build_error(const char *code, const char *description, char *buffer, size_t buffer_size);
int protocol_build_result(const char *query_type, const char *payload, char *buffer, size_t buffer_size);
int protocol_build_statusr(size_t sensors, size_t operators, unsigned long uptime, char *buffer, size_t buffer_size);
int protocol_sanitize_request_for_log(const char *request, char *buffer, size_t buffer_size);
int protocol_handle_request(
    protocol_context_t *context,
    client_session_t *session,
    const char *request,
    char *response,
    size_t response_size,
    protocol_result_t *result
);

#endif
