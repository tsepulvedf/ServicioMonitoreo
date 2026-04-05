#include "network_thread.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

constexpr int kReadTimeoutSeconds = 30;
constexpr int kMaxReconnectAttempts = 5;
constexpr int kSelectTickMilliseconds = 1000;

#ifdef _WIN32
constexpr std::intptr_t kInvalidSocketHandle = static_cast<std::intptr_t>(INVALID_SOCKET);
#else
constexpr std::intptr_t kInvalidSocketHandle = static_cast<std::intptr_t>(-1);
#endif

std::string trimCopy(const std::string &value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();

    if (first >= last) {
        return "";
    }
    return std::string(first, last);
}

std::vector<std::string> splitMessage(const std::string &message, std::size_t max_parts = 0) {
    std::vector<std::string> parts;
    std::size_t start = 0;

    while (start <= message.size()) {
        if (max_parts != 0 && parts.size() + 1 == max_parts) {
            parts.push_back(message.substr(start));
            break;
        }

        const std::size_t separator = message.find('|', start);
        if (separator == std::string::npos) {
            parts.push_back(message.substr(start));
            break;
        }

        parts.push_back(message.substr(start, separator - start));
        start = separator + 1;
    }

    return parts;
}

bool isAsciiText(const std::string &value) {
    for (unsigned char ch : value) {
        if (ch < 0x20 || ch > 0x7e) {
            return false;
        }
    }
    return true;
}

bool parseQuotedString(const std::string &text, std::size_t &index, std::string &output) {
    if (index >= text.size() || text[index] != '"') {
        return false;
    }

    ++index;
    std::ostringstream builder;
    while (index < text.size()) {
        const char current = text[index++];
        if (current == '"') {
            output = builder.str();
            return true;
        }
        if (current == '\\') {
            if (index >= text.size()) {
                return false;
            }
            builder << text[index++];
            continue;
        }
        builder << current;
    }

    return false;
}

void skipWhitespace(const std::string &text, std::size_t &index) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }
}

bool parseObjectArray(
    const std::string &payload,
    std::vector<std::unordered_map<std::string, std::string>> &objects
) {
    std::size_t index = 0;
    skipWhitespace(payload, index);
    if (index >= payload.size() || payload[index] != '[') {
        return false;
    }

    ++index;
    skipWhitespace(payload, index);
    while (index < payload.size() && payload[index] != ']') {
        std::unordered_map<std::string, std::string> object;

        if (payload[index] != '{') {
            return false;
        }
        ++index;
        skipWhitespace(payload, index);

        while (index < payload.size() && payload[index] != '}') {
            std::string key;
            std::string value;

            if (!parseQuotedString(payload, index, key)) {
                return false;
            }
            skipWhitespace(payload, index);
            if (index >= payload.size() || payload[index] != ':') {
                return false;
            }
            ++index;
            skipWhitespace(payload, index);

            if (index < payload.size() && payload[index] == '"') {
                if (!parseQuotedString(payload, index, value)) {
                    return false;
                }
            } else {
                const std::size_t value_start = index;
                while (index < payload.size() && payload[index] != ',' && payload[index] != '}') {
                    ++index;
                }
                value = trimCopy(payload.substr(value_start, index - value_start));
            }

            object[key] = value;
            skipWhitespace(payload, index);
            if (index < payload.size() && payload[index] == ',') {
                ++index;
                skipWhitespace(payload, index);
            }
        }

        if (index >= payload.size() || payload[index] != '}') {
            return false;
        }

        ++index;
        objects.push_back(std::move(object));
        skipWhitespace(payload, index);
        if (index < payload.size() && payload[index] == ',') {
            ++index;
            skipWhitespace(payload, index);
        }
    }

    return index < payload.size() && payload[index] == ']';
}

std::vector<SensorSnapshot> parseSensorsPayload(const std::string &payload) {
    std::vector<SensorSnapshot> sensors;
    std::vector<std::unordered_map<std::string, std::string>> objects;

    if (!parseObjectArray(payload, objects)) {
        return sensors;
    }

    for (const auto &object : objects) {
        SensorSnapshot snapshot;
        auto id_it = object.find("sensor_id");
        auto type_it = object.find("tipo");
        auto value_it = object.find("ultimo_valor");
        auto timestamp_it = object.find("ultimo_timestamp");

        snapshot.sensor_id = id_it != object.end() ? id_it->second : "";
        snapshot.type = type_it != object.end() ? type_it->second : "";
        snapshot.value = value_it != object.end() ? value_it->second : "";
        snapshot.timestamp = timestamp_it != object.end() ? timestamp_it->second : "";
        sensors.push_back(snapshot);
    }

    return sensors;
}

std::vector<SensorSnapshot> parseMeasurementsPayload(const std::string &payload) {
    std::vector<SensorSnapshot> measurements;
    std::vector<std::unordered_map<std::string, std::string>> objects;

    if (!parseObjectArray(payload, objects)) {
        return measurements;
    }

    for (const auto &object : objects) {
        SensorSnapshot snapshot;
        auto id_it = object.find("sensor_id");
        auto type_it = object.find("tipo");
        auto value_it = object.find("valor");
        auto timestamp_it = object.find("timestamp");

        snapshot.sensor_id = id_it != object.end() ? id_it->second : "";
        snapshot.type = type_it != object.end() ? type_it->second : "";
        snapshot.value = value_it != object.end() ? value_it->second : "";
        snapshot.timestamp = timestamp_it != object.end() ? timestamp_it->second : "";
        measurements.push_back(snapshot);
    }

    return measurements;
}

#ifdef _WIN32
bool ensureSocketLayer() {
    static std::once_flag once_flag;
    static bool ready = false;

    std::call_once(once_flag, []() {
        WSADATA wsa_data;
        ready = WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    });

    return ready;
}
#else
bool ensureSocketLayer() {
    return true;
}
#endif

void closeSocketHandle(std::intptr_t handle) {
    if (handle == kInvalidSocketHandle) {
        return;
    }

#ifdef _WIN32
    closesocket(static_cast<SOCKET>(handle));
#else
    close(static_cast<int>(handle));
#endif
}

void shutdownSocketHandle(std::intptr_t handle) {
    if (handle == kInvalidSocketHandle) {
        return;
    }

#ifdef _WIN32
    shutdown(static_cast<SOCKET>(handle), SD_BOTH);
#else
    shutdown(static_cast<int>(handle), SHUT_RDWR);
#endif
}

bool sendAll(std::intptr_t handle, const std::string &message) {
    std::size_t sent = 0;

    while (sent < message.size()) {
#ifdef _WIN32
        const int result = send(
            static_cast<SOCKET>(handle),
            message.data() + sent,
            static_cast<int>(message.size() - sent),
            0
        );
#else
        const ssize_t result = send(
            static_cast<int>(handle),
            message.data() + sent,
            message.size() - sent,
            0
        );
#endif
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }

    return true;
}

bool receiveLineWithTimeout(std::intptr_t handle, std::string &line) {
    std::string buffer;
    auto last_activity = std::chrono::steady_clock::now();

    while (true) {
        fd_set read_set;
        FD_ZERO(&read_set);
#ifdef _WIN32
        FD_SET(static_cast<SOCKET>(handle), &read_set);
#else
        FD_SET(static_cast<int>(handle), &read_set);
#endif

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

#ifdef _WIN32
        const int select_result = select(0, &read_set, nullptr, nullptr, &timeout);
#else
        const int select_result = select(static_cast<int>(handle) + 1, &read_set, nullptr, nullptr, &timeout);
#endif

        if (select_result < 0) {
            return false;
        }

        if (select_result == 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - last_activity
            );
            if (elapsed.count() >= kReadTimeoutSeconds) {
                return false;
            }
            continue;
        }

        std::array<char, 256> chunk {};
#ifdef _WIN32
        const int received = recv(static_cast<SOCKET>(handle), chunk.data(), static_cast<int>(chunk.size()), 0);
#else
        const ssize_t received = recv(static_cast<int>(handle), chunk.data(), chunk.size(), 0);
#endif
        if (received <= 0) {
            return false;
        }

        buffer.append(chunk.data(), static_cast<std::size_t>(received));
        last_activity = std::chrono::steady_clock::now();

        const std::size_t line_end = buffer.find("\r\n");
        if (line_end != std::string::npos) {
            line = buffer.substr(0, line_end);
            return isAsciiText(line);
        }
    }
}

}  // namespace

NetworkThread::NetworkThread(
    std::string host,
    std::string port,
    std::string username,
    std::string password
) :
    host_(std::move(host)),
    port_(std::move(port)),
    username_(std::move(username)),
    password_(std::move(password)),
    running_(false),
    manual_disconnect_(false),
    connection_state_(ConnectionState::Disconnected),
    socket_handle_(kInvalidSocketHandle) {
}

NetworkThread::~NetworkThread() {
    disconnect();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void NetworkThread::start() {
    if (running_.exchange(true)) {
        return;
    }

    manual_disconnect_ = false;
    worker_ = std::thread(&NetworkThread::threadMain, this);
}

void NetworkThread::sendQuery(const std::string &target) {
    sendMessage("QUERY|" + target + "\r\n");
}

void NetworkThread::sendStatus() {
    sendMessage("STATUS\r\n");
}

void NetworkThread::disconnect() {
    std::intptr_t current_socket;

    manual_disconnect_ = true;
    running_ = false;

    {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        current_socket = socket_handle_;
    }

    if (current_socket != kInvalidSocketHandle) {
        sendMessage("DISCONNECT|" + username_ + "\r\n");
    }

    std::lock_guard<std::mutex> lock(connection_mutex_);
    shutdownSocketHandle(socket_handle_);
    closeSocketHandle(socket_handle_);
    socket_handle_ = kInvalidSocketHandle;
}

std::vector<NetworkEvent> NetworkThread::takePendingEvents() {
    std::vector<NetworkEvent> pending_events;
    std::lock_guard<std::mutex> lock(event_mutex_);

    while (!events_.empty()) {
        pending_events.push_back(std::move(events_.front()));
        events_.pop();
    }

    return pending_events;
}

ConnectionState NetworkThread::connectionState() const {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    return connection_state_;
}

void NetworkThread::threadMain() {
    int attempt = 0;

    while (running_) {
        const ConnectAttemptResult result = connectAndRegister();
        if (result == ConnectAttemptResult::FatalFailure) {
            break;
        }

        if (result == ConnectAttemptResult::RetryableFailure) {
            {
                std::lock_guard<std::mutex> lock(connection_mutex_);
                shutdownSocketHandle(socket_handle_);
                closeSocketHandle(socket_handle_);
                socket_handle_ = kInvalidSocketHandle;
            }
            attempt++;
            if (attempt > kMaxReconnectAttempts || !running_) {
                enqueueEvent({NetworkEventType::ErrorMessage, ConnectionState::Disconnected, {}, {}, {}, {}, "Se agotaron los reintentos de conexion"});
                break;
            }

            setConnectionState(ConnectionState::Reconnecting, "Reconectando...");
            std::this_thread::sleep_for(std::chrono::seconds(std::min(1 << (attempt - 1), 16)));
            continue;
        }

        attempt = 0;
        if (!receiveLoop()) {
            {
                std::lock_guard<std::mutex> lock(connection_mutex_);
                shutdownSocketHandle(socket_handle_);
                closeSocketHandle(socket_handle_);
                socket_handle_ = kInvalidSocketHandle;
            }
            if (manual_disconnect_ || !running_) {
                break;
            }

            attempt++;
            if (attempt > kMaxReconnectAttempts) {
                enqueueEvent({NetworkEventType::ErrorMessage, ConnectionState::Disconnected, {}, {}, {}, {}, "Se perdio la conexion y no fue posible recuperarla"});
                break;
            }

            setConnectionState(ConnectionState::Reconnecting, "Conexion perdida, reintentando...");
            std::this_thread::sleep_for(std::chrono::seconds(std::min(1 << (attempt - 1), 16)));
        }
    }

    setConnectionState(ConnectionState::Disconnected, "Desconectado");
}

NetworkThread::ConnectAttemptResult NetworkThread::connectAndRegister() {
    if (!ensureSocketLayer()) {
        enqueueEvent({NetworkEventType::ErrorMessage, ConnectionState::Disconnected, {}, {}, {}, {}, "No fue posible inicializar la capa de sockets"});
        return ConnectAttemptResult::FatalFailure;
    }

    setConnectionState(ConnectionState::Connecting, "Conectando...");

    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo *results = nullptr;
    if (getaddrinfo(host_.c_str(), port_.c_str(), &hints, &results) != 0) {
        enqueueEvent({NetworkEventType::ErrorMessage, ConnectionState::Disconnected, {}, {}, {}, {}, "No fue posible resolver el hostname del servidor"});
        return ConnectAttemptResult::RetryableFailure;
    }

    std::intptr_t connected_socket = kInvalidSocketHandle;
    for (addrinfo *current = results; current != nullptr; current = current->ai_next) {
#ifdef _WIN32
        const SOCKET candidate = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (candidate == INVALID_SOCKET) {
            continue;
        }
        if (connect(candidate, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) {
            connected_socket = static_cast<std::intptr_t>(candidate);
            break;
        }
        closesocket(candidate);
#else
        const int candidate = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (candidate < 0) {
            continue;
        }
        if (connect(candidate, current->ai_addr, current->ai_addrlen) == 0) {
            connected_socket = static_cast<std::intptr_t>(candidate);
            break;
        }
        close(candidate);
#endif
    }

    freeaddrinfo(results);

    if (connected_socket == kInvalidSocketHandle) {
        enqueueEvent({NetworkEventType::ErrorMessage, ConnectionState::Disconnected, {}, {}, {}, {}, "No fue posible abrir la conexion TCP al servidor"});
        return ConnectAttemptResult::RetryableFailure;
    }

    {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        socket_handle_ = connected_socket;
    }

    if (!sendMessage("REGISTER|operator|" + username_ + "|" + password_ + "\r\n")) {
        closeSocketHandle(connected_socket);
        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            socket_handle_ = kInvalidSocketHandle;
        }
        return ConnectAttemptResult::RetryableFailure;
    }

    std::string response;
    if (!receiveLineWithTimeout(connected_socket, response)) {
        enqueueEvent({NetworkEventType::ErrorMessage, ConnectionState::Disconnected, {}, {}, {}, {}, "Timeout o error esperando ACK de registro"});
        closeSocketHandle(connected_socket);
        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            socket_handle_ = kInvalidSocketHandle;
        }
        return ConnectAttemptResult::RetryableFailure;
    }

    const std::vector<std::string> parts = splitMessage(response, 3);
    if (!parts.empty() && parts[0] == "ACK") {
        setConnectionState(ConnectionState::Connected, "Conectado");
        enqueueEvent({NetworkEventType::InfoMessage, ConnectionState::Connected, {}, {}, {}, {}, "Registro de operador exitoso"});
        return ConnectAttemptResult::Success;
    }

    closeSocketHandle(connected_socket);
    {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        socket_handle_ = kInvalidSocketHandle;
    }

    if (!parts.empty() && parts[0] == "ERROR") {
        const std::string error_code = parts.size() > 1 ? parts[1] : "UNKNOWN";
        const std::string error_message = parts.size() > 2 ? parts[2] : "Registro rechazado";
        enqueueEvent({NetworkEventType::ErrorMessage, ConnectionState::Disconnected, {}, {}, {}, {}, "Registro rechazado: " + error_code + " - " + error_message});

        if (error_code == "INVALID_CREDENTIALS" || error_code == "ROLE_MISMATCH") {
            return ConnectAttemptResult::FatalFailure;
        }
        return ConnectAttemptResult::RetryableFailure;
    }

    enqueueEvent({NetworkEventType::ErrorMessage, ConnectionState::Disconnected, {}, {}, {}, {}, "Respuesta inesperada durante REGISTER"});
    return ConnectAttemptResult::RetryableFailure;
}

bool NetworkThread::receiveLoop() {
    std::string buffer;
    auto last_activity = std::chrono::steady_clock::now();

    while (running_ && !manual_disconnect_) {
        std::intptr_t current_socket;
        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            current_socket = socket_handle_;
        }

        if (current_socket == kInvalidSocketHandle) {
            return false;
        }

        fd_set read_set;
        FD_ZERO(&read_set);
#ifdef _WIN32
        FD_SET(static_cast<SOCKET>(current_socket), &read_set);
#else
        FD_SET(static_cast<int>(current_socket), &read_set);
#endif

        timeval timeout;
        timeout.tv_sec = kSelectTickMilliseconds / 1000;
        timeout.tv_usec = 0;

#ifdef _WIN32
        const int select_result = select(0, &read_set, nullptr, nullptr, &timeout);
#else
        const int select_result = select(static_cast<int>(current_socket) + 1, &read_set, nullptr, nullptr, &timeout);
#endif

        if (select_result < 0) {
            enqueueEvent({NetworkEventType::ErrorMessage, ConnectionState::Connected, {}, {}, {}, {}, "Error esperando datos del servidor"});
            return false;
        }

        if (select_result == 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - last_activity
            );
            if (elapsed.count() >= kReadTimeoutSeconds) {
                enqueueEvent({NetworkEventType::ErrorMessage, ConnectionState::Connected, {}, {}, {}, {}, "Timeout de lectura de 30 segundos"});
                return false;
            }
            continue;
        }

        std::array<char, 512> chunk {};
#ifdef _WIN32
        const int received = recv(static_cast<SOCKET>(current_socket), chunk.data(), static_cast<int>(chunk.size()), 0);
#else
        const ssize_t received = recv(static_cast<int>(current_socket), chunk.data(), chunk.size(), 0);
#endif
        if (received <= 0) {
            enqueueEvent({NetworkEventType::ErrorMessage, ConnectionState::Connected, {}, {}, {}, {}, "El servidor cerro la conexion"});
            return false;
        }

        buffer.append(chunk.data(), static_cast<std::size_t>(received));
        last_activity = std::chrono::steady_clock::now();

        std::size_t line_end = buffer.find("\r\n");
        while (line_end != std::string::npos) {
            const std::string line = buffer.substr(0, line_end);
            buffer.erase(0, line_end + 2);

            if (!isAsciiText(line)) {
                enqueueEvent({NetworkEventType::ErrorMessage, ConnectionState::Connected, {}, {}, {}, {}, "Mensaje no ASCII recibido desde el servidor"});
                return false;
            }

            const std::string opcode = line.substr(0, line.find('|'));
            const std::vector<std::string> parts = opcode == "RESULT"
                ? splitMessage(line, 3)
                : splitMessage(line);
            if (parts.empty()) {
                line_end = buffer.find("\r\n");
                continue;
            }

            if (parts[0] == "ALERT" && parts.size() == 6) {
                NetworkEvent event;
                event.type = NetworkEventType::AlertReceived;
                event.connection_state = ConnectionState::Connected;
                event.alert.sensor_id = parts[1];
                event.alert.type = parts[2];
                event.alert.value = parts[3];
                event.alert.threshold = parts[4];
                event.alert.timestamp = parts[5];
                enqueueEvent(event);
            } else if (parts[0] == "RESULT" && parts.size() == 3) {
                if (parts[1] == "SENSORS") {
                    NetworkEvent event;
                    event.type = NetworkEventType::SensorsUpdated;
                    event.connection_state = ConnectionState::Connected;
                    event.sensors = parseSensorsPayload(parts[2]);
                    enqueueEvent(event);
                } else if (parts[1] == "MEASUREMENTS") {
                    NetworkEvent event;
                    event.type = NetworkEventType::MeasurementsUpdated;
                    event.connection_state = ConnectionState::Connected;
                    event.measurements = parseMeasurementsPayload(parts[2]);
                    enqueueEvent(event);
                } else {
                    enqueueEvent({NetworkEventType::InfoMessage, ConnectionState::Connected, {}, {}, {}, {}, "Resultado recibido para " + parts[1]});
                }
            } else if (parts[0] == "STATUSR" && parts.size() == 4) {
                NetworkEvent event;
                event.type = NetworkEventType::StatusUpdated;
                event.connection_state = ConnectionState::Connected;
                event.status.active_sensors = parts[1];
                event.status.active_operators = parts[2];
                event.status.uptime_seconds = parts[3];
                enqueueEvent(event);
            } else if (parts[0] == "ERROR") {
                const std::string error_message = parts.size() > 2 ? parts[1] + " - " + parts[2] : line;
                enqueueEvent({NetworkEventType::ErrorMessage, ConnectionState::Connected, {}, {}, {}, {}, error_message});
            } else if (parts[0] == "ACK") {
                enqueueEvent({NetworkEventType::InfoMessage, ConnectionState::Connected, {}, {}, {}, {}, line});
            } else {
                enqueueEvent({NetworkEventType::InfoMessage, ConnectionState::Connected, {}, {}, {}, {}, "Mensaje recibido: " + line});
            }

            line_end = buffer.find("\r\n");
        }
    }

    return false;
}

bool NetworkThread::sendMessage(const std::string &message) {
    std::lock_guard<std::mutex> send_lock(send_mutex_);
    std::intptr_t current_socket;

    {
        std::lock_guard<std::mutex> connection_lock(connection_mutex_);
        current_socket = socket_handle_;
    }

    if (current_socket == kInvalidSocketHandle) {
        enqueueEvent({NetworkEventType::ErrorMessage, connectionState(), {}, {}, {}, {}, "No hay conexion activa con el servidor"});
        return false;
    }

    if (!sendAll(current_socket, message)) {
        enqueueEvent({NetworkEventType::ErrorMessage, connectionState(), {}, {}, {}, {}, "Fallo enviando mensaje al servidor"});
        return false;
    }

    return true;
}

void NetworkThread::enqueueEvent(const NetworkEvent &event) {
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        events_.push(event);
    }
    event_cv_.notify_all();
}

void NetworkThread::setConnectionState(ConnectionState new_state, const std::string &message) {
    {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        connection_state_ = new_state;
    }

    NetworkEvent event;
    event.type = NetworkEventType::ConnectionStateChanged;
    event.connection_state = new_state;
    event.message = message;
    enqueueEvent(event);
}
