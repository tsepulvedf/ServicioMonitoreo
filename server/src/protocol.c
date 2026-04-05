#define _POSIX_C_SOURCE 200809L

#include "protocol.h"

#include "auth_client.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_ascii_message(const char *raw_message) {
    const unsigned char *current = (const unsigned char *)raw_message;

    while (*current != '\0') {
        if (*current == '\r' || *current == '\n') {
            current++;
            continue;
        }
        if (*current < 0x20 || *current > 0x7e) {
            return 0;
        }
        current++;
    }

    return 1;
}

static int valid_timestamp(const char *timestamp) {
    size_t length;
    size_t i;

    if (timestamp == NULL) {
        return 0;
    }

    length = strlen(timestamp);
    if (length < 20) {
        return 0;
    }

    for (i = 0; i < length; i++) {
        char ch = timestamp[i];
        if (!(isdigit((unsigned char)ch) || ch == '-' || ch == ':' || ch == 'T' || ch == 'Z' || ch == '+' || ch == '.')) {
            return 0;
        }
    }

    return strchr(timestamp, 'T') != NULL;
}

static int parse_decimal(const char *text, double *value) {
    char *endptr;

    if (text == NULL || value == NULL || strchr(text, ',') != NULL) {
        return 0;
    }

    *value = strtod(text, &endptr);
    return *text != '\0' && endptr != NULL && *endptr == '\0';
}

static client_role_t role_from_text(const char *role_text) {
    if (strcmp(role_text, "sensor") == 0) {
        return CLIENT_ROLE_SENSOR;
    }
    if (strcmp(role_text, "operator") == 0) {
        return CLIENT_ROLE_OPERATOR;
    }
    return CLIENT_ROLE_NONE;
}

const char *protocol_opcode_name(iotp_opcode_t opcode) {
    switch (opcode) {
        case IOTP_OPCODE_REGISTER:
            return "REGISTER";
        case IOTP_OPCODE_ACK:
            return "ACK";
        case IOTP_OPCODE_ERROR:
            return "ERROR";
        case IOTP_OPCODE_DATA:
            return "DATA";
        case IOTP_OPCODE_ALERT:
            return "ALERT";
        case IOTP_OPCODE_QUERY:
            return "QUERY";
        case IOTP_OPCODE_RESULT:
            return "RESULT";
        case IOTP_OPCODE_STATUS:
            return "STATUS";
        case IOTP_OPCODE_STATUSR:
            return "STATUSR";
        case IOTP_OPCODE_DISCONNECT:
            return "DISCONNECT";
        default:
            return "UNKNOWN";
    }
}

int protocol_validate_opcode(const char *opcode_text, iotp_opcode_t *opcode) {
    if (opcode_text == NULL || opcode == NULL) {
        return -1;
    }

    if (strcmp(opcode_text, "REGISTER") == 0) {
        *opcode = IOTP_OPCODE_REGISTER;
    } else if (strcmp(opcode_text, "ACK") == 0) {
        *opcode = IOTP_OPCODE_ACK;
    } else if (strcmp(opcode_text, "ERROR") == 0) {
        *opcode = IOTP_OPCODE_ERROR;
    } else if (strcmp(opcode_text, "DATA") == 0) {
        *opcode = IOTP_OPCODE_DATA;
    } else if (strcmp(opcode_text, "ALERT") == 0) {
        *opcode = IOTP_OPCODE_ALERT;
    } else if (strcmp(opcode_text, "QUERY") == 0) {
        *opcode = IOTP_OPCODE_QUERY;
    } else if (strcmp(opcode_text, "RESULT") == 0) {
        *opcode = IOTP_OPCODE_RESULT;
    } else if (strcmp(opcode_text, "STATUS") == 0) {
        *opcode = IOTP_OPCODE_STATUS;
    } else if (strcmp(opcode_text, "STATUSR") == 0) {
        *opcode = IOTP_OPCODE_STATUSR;
    } else if (strcmp(opcode_text, "DISCONNECT") == 0) {
        *opcode = IOTP_OPCODE_DISCONNECT;
    } else {
        *opcode = IOTP_OPCODE_UNKNOWN;
        return 1;
    }

    return 0;
}

int protocol_parse_message(const char *raw_message, protocol_message_t *message) {
    char *saveptr;
    char *token;
    size_t length;

    if (raw_message == NULL || message == NULL) {
        return -1;
    }

    memset(message, 0, sizeof(*message));
    length = strlen(raw_message);
    if (length < 2 || strcmp(raw_message + length - 2, "\r\n") != 0) {
        return -1;
    }
    if (length >= sizeof(message->storage) || !is_ascii_message(raw_message)) {
        return -1;
    }

    memcpy(message->storage, raw_message, length - 2);
    message->storage[length - 2] = '\0';

    token = strtok_r(message->storage, "|", &saveptr);
    if (token == NULL || token[0] == '\0') {
        return -1;
    }

    snprintf(message->opcode_text, sizeof(message->opcode_text), "%s", token);
    protocol_validate_opcode(message->opcode_text, &message->opcode);

    while ((token = strtok_r(NULL, "|", &saveptr)) != NULL) {
        if (message->arg_count >= IOTP_MAX_ARGS || token[0] == '\0') {
            return -1;
        }
        message->args[message->arg_count++] = token;
    }

    return 0;
}

int protocol_build_ack(const char *message, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0 || message == NULL) {
        return -1;
    }

    return snprintf(buffer, buffer_size, "ACK|%s\r\n", message) >= (int)buffer_size ? -1 : 0;
}

int protocol_build_error(const char *code, const char *description, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0 || code == NULL || description == NULL) {
        return -1;
    }

    return snprintf(buffer, buffer_size, "ERROR|%s|%s\r\n", code, description) >= (int)buffer_size ? -1 : 0;
}

int protocol_build_result(const char *query_type, const char *payload, char *buffer, size_t buffer_size) {
    if (buffer == NULL || query_type == NULL || payload == NULL || buffer_size == 0) {
        return -1;
    }

    return snprintf(buffer, buffer_size, "RESULT|%s|%s\r\n", query_type, payload) >= (int)buffer_size ? -1 : 0;
}

int protocol_build_statusr(size_t sensors, size_t operators, unsigned long uptime, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

    return snprintf(buffer, buffer_size, "STATUSR|%zu|%zu|%lu\r\n", sensors, operators, uptime) >= (int)buffer_size ? -1 : 0;
}

int protocol_sanitize_request_for_log(const char *request, char *buffer, size_t buffer_size) {
    protocol_message_t message;

    if (request == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }

    if (protocol_parse_message(request, &message) != 0) {
        snprintf(buffer, buffer_size, "%s", request);
        return 0;
    }

    if (message.opcode == IOTP_OPCODE_REGISTER && message.arg_count == 3) {
        snprintf(buffer, buffer_size, "REGISTER|%s|%s|<redacted>", message.args[0], message.args[1]);
        return 0;
    }

    snprintf(buffer, buffer_size, "%s", message.storage);
    return 0;
}

static int require_authenticated(const client_session_t *session, char *response, size_t response_size) {
    if (!session->authenticated) {
        return protocol_build_error("NOT_AUTHENTICATED", "REGISTER required", response, response_size);
    }
    return 0;
}

static int handle_register(
    protocol_context_t *context,
    client_session_t *session,
    const protocol_message_t *message,
    char *response,
    size_t response_size
) {
    auth_result_t auth_result;
    client_role_t requested_role;
    char returned_role[32];
    char reason[128];

    if (message->arg_count != 3) {
        return protocol_build_error("INVALID_FIELD_COUNT", "REGISTER expects 3 fields", response, response_size);
    }
    if (session->authenticated) {
        return protocol_build_error("FORBIDDEN_OP", "already authenticated", response, response_size);
    }

    requested_role = role_from_text(message->args[0]);
    if (requested_role == CLIENT_ROLE_NONE) {
        return protocol_build_error("INVALID_ROLE", "unsupported role", response, response_size);
    }

    auth_result = auth_client_authenticate(
        context->auth_host,
        context->auth_port,
        message->args[1],
        message->args[2],
        returned_role,
        sizeof(returned_role),
        reason,
        sizeof(reason)
    );

    if (auth_result == AUTH_RESULT_UNAVAILABLE || auth_result == AUTH_RESULT_PROTOCOL_ERROR) {
        return protocol_build_error("AUTH_UNAVAILABLE", "auth service unavailable", response, response_size);
    }
    if (auth_result == AUTH_RESULT_DENIED) {
        return protocol_build_error("INVALID_CREDENTIALS", "authentication failed", response, response_size);
    }
    if (requested_role != role_from_text(returned_role)) {
        return protocol_build_error("ROLE_MISMATCH", "role mismatch", response, response_size);
    }

    session->authenticated = true;
    session->role = requested_role;
    snprintf(session->client_id, sizeof(session->client_id), "%s", message->args[1]);

    if (requested_role == CLIENT_ROLE_SENSOR) {
        sensor_manager_register_sensor(context->sensor_manager, session->client_id);
        return protocol_build_ack("Registered as sensor", response, response_size);
    }

    sensor_manager_register_operator(context->sensor_manager, session->client_id);
    return protocol_build_ack("Registered as operator", response, response_size);
}

static int handle_data(
    protocol_context_t *context,
    client_session_t *session,
    const protocol_message_t *message,
    char *response,
    size_t response_size,
    protocol_result_t *result
) {
    double value;
    int anomaly;
    double threshold;

    if (require_authenticated(session, response, response_size) != 0) {
        return 0;
    }
    if (session->role != CLIENT_ROLE_SENSOR) {
        return protocol_build_error("FORBIDDEN_OP", "sensor only", response, response_size);
    }
    if (message->arg_count != 4) {
        return protocol_build_error("INVALID_FIELD_COUNT", "DATA expects 4 fields", response, response_size);
    }
    if (strcmp(message->args[0], session->client_id) != 0) {
        return protocol_build_error("ID_MISMATCH", "sensor id mismatch", response, response_size);
    }
    if (strcmp(message->args[1], "temperatura") != 0 &&
        strcmp(message->args[1], "vibracion") != 0 &&
        strcmp(message->args[1], "energia") != 0) {
        return protocol_build_error("INVALID_SENSOR_TYPE", "invalid sensor type", response, response_size);
    }
    if (!parse_decimal(message->args[2], &value)) {
        return protocol_build_error("INVALID_VALUE", "invalid numeric value", response, response_size);
    }
    if (!valid_timestamp(message->args[3])) {
        return protocol_build_error("INVALID_TIMESTAMP", "invalid timestamp", response, response_size);
    }

    if (sensor_manager_record_measurement(
            context->sensor_manager,
            message->args[0],
            message->args[1],
            value,
            message->args[3],
            &anomaly,
            &threshold
        ) != 0) {
        return protocol_build_error("INTERNAL_ERROR", "measurement store failed", response, response_size);
    }

    if (anomaly) {
        result->generated_alert = true;
        snprintf(
            result->alert_message,
            sizeof(result->alert_message),
            "ALERT|%s|%s|%.2f|%.2f|%s\r\n",
            message->args[0],
            message->args[1],
            value,
            threshold,
            message->args[3]
        );
    }

    return protocol_build_ack("Data received", response, response_size);
}

static int handle_query(
    protocol_context_t *context,
    client_session_t *session,
    const protocol_message_t *message,
    char *response,
    size_t response_size
) {
    char payload[IOTP_MAX_QUERY_PAYLOAD];

    if (require_authenticated(session, response, response_size) != 0) {
        return 0;
    }
    if (session->role != CLIENT_ROLE_OPERATOR) {
        return protocol_build_error("FORBIDDEN_OP", "operator only", response, response_size);
    }
    if (message->arg_count != 1) {
        return protocol_build_error("INVALID_FIELD_COUNT", "QUERY expects 1 field", response, response_size);
    }
    if (strcmp(message->args[0], "SENSORS") != 0 &&
        strcmp(message->args[0], "MEASUREMENTS") != 0 &&
        strcmp(message->args[0], "ALERTS") != 0) {
        return protocol_build_error("INVALID_TARGET", "invalid query target", response, response_size);
    }
    if (sensor_manager_build_query_result(context->sensor_manager, message->args[0], payload, sizeof(payload)) != 0) {
        return protocol_build_error("INTERNAL_ERROR", "query build failed", response, response_size);
    }
    return protocol_build_result(message->args[0], payload, response, response_size);
}

static int handle_status(
    protocol_context_t *context,
    client_session_t *session,
    const protocol_message_t *message,
    char *response,
    size_t response_size
) {
    size_t active_sensors;
    size_t active_operators;
    time_t now;

    if (require_authenticated(session, response, response_size) != 0) {
        return 0;
    }
    if (session->role != CLIENT_ROLE_OPERATOR) {
        return protocol_build_error("FORBIDDEN_OP", "operator only", response, response_size);
    }
    if (message->arg_count != 0) {
        return protocol_build_error("INVALID_FIELD_COUNT", "STATUS expects 0 fields", response, response_size);
    }
    if (sensor_manager_get_status(context->sensor_manager, &active_sensors, &active_operators) != 0) {
        return protocol_build_error("INTERNAL_ERROR", "status build failed", response, response_size);
    }

    now = time(NULL);
    return protocol_build_statusr(
        active_sensors,
        active_operators,
        (unsigned long)(now - context->server_start_time),
        response,
        response_size
    );
}

static int handle_disconnect(
    protocol_context_t *context,
    client_session_t *session,
    const protocol_message_t *message,
    char *response,
    size_t response_size,
    protocol_result_t *result
) {
    if (require_authenticated(session, response, response_size) != 0) {
        return 0;
    }
    if (message->arg_count != 1) {
        return protocol_build_error("INVALID_FIELD_COUNT", "DISCONNECT expects 1 field", response, response_size);
    }
    if (strcmp(message->args[0], session->client_id) != 0) {
        return protocol_build_error("ID_MISMATCH", "disconnect id mismatch", response, response_size);
    }

    if (session->role == CLIENT_ROLE_SENSOR) {
        sensor_manager_unregister_sensor(context->sensor_manager, session->client_id);
    } else if (session->role == CLIENT_ROLE_OPERATOR) {
        sensor_manager_unregister_operator(context->sensor_manager, session->client_id);
    }

    session->authenticated = false;
    session->role = CLIENT_ROLE_NONE;
    session->client_id[0] = '\0';
    result->close_connection = true;
    return protocol_build_ack("Disconnected", response, response_size);
}

int protocol_handle_request(
    protocol_context_t *context,
    client_session_t *session,
    const char *request,
    char *response,
    size_t response_size,
    protocol_result_t *result
) {
    protocol_message_t message;

    if (context == NULL || session == NULL || request == NULL || response == NULL || result == NULL) {
        return -1;
    }

    memset(result, 0, sizeof(*result));

    if (protocol_parse_message(request, &message) != 0) {
        return protocol_build_error("MALFORMED_MESSAGE", "invalid frame", response, response_size);
    }

    if (message.opcode == IOTP_OPCODE_UNKNOWN) {
        return protocol_build_error("UNKNOWN_OP", message.opcode_text, response, response_size);
    }

    switch (message.opcode) {
        case IOTP_OPCODE_REGISTER:
            return handle_register(context, session, &message, response, response_size);
        case IOTP_OPCODE_DATA:
            return handle_data(context, session, &message, response, response_size, result);
        case IOTP_OPCODE_QUERY:
            return handle_query(context, session, &message, response, response_size);
        case IOTP_OPCODE_STATUS:
            return handle_status(context, session, &message, response, response_size);
        case IOTP_OPCODE_DISCONNECT:
            return handle_disconnect(context, session, &message, response, response_size, result);
        case IOTP_OPCODE_ACK:
        case IOTP_OPCODE_ERROR:
        case IOTP_OPCODE_ALERT:
        case IOTP_OPCODE_RESULT:
        case IOTP_OPCODE_STATUSR:
            return protocol_build_error("FORBIDDEN_OP", "server-only opcode", response, response_size);
        default:
            return protocol_build_error("UNKNOWN_OP", message.opcode_text, response, response_size);
    }
}
