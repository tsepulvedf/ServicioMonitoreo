#ifndef NETWORK_THREAD_H
#define NETWORK_THREAD_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting
};

enum class NetworkEventType {
    ConnectionStateChanged,
    SensorsUpdated,
    MeasurementsUpdated,
    AlertReceived,
    StatusUpdated,
    ErrorMessage,
    InfoMessage
};

struct SensorSnapshot {
    std::string sensor_id;
    std::string type;
    std::string value;
    std::string timestamp;
};

struct AlertSnapshot {
    std::string sensor_id;
    std::string type;
    std::string value;
    std::string threshold;
    std::string timestamp;
};

struct StatusSnapshot {
    std::string active_sensors;
    std::string active_operators;
    std::string uptime_seconds;
};

struct NetworkEvent {
    NetworkEventType type = NetworkEventType::InfoMessage;
    ConnectionState connection_state = ConnectionState::Disconnected;
    std::vector<SensorSnapshot> sensors;
    std::vector<SensorSnapshot> measurements;
    AlertSnapshot alert;
    StatusSnapshot status;
    std::string message;
};

class NetworkThread {
public:
    NetworkThread(
        std::string host,
        std::string port,
        std::string username,
        std::string password
    );
    ~NetworkThread();

    void start();
    void sendQuery(const std::string &target);
    void sendStatus();
    void disconnect();

    std::vector<NetworkEvent> takePendingEvents();
    ConnectionState connectionState() const;

private:
    enum class ConnectAttemptResult {
        Success,
        RetryableFailure,
        FatalFailure
    };

    void threadMain();
    ConnectAttemptResult connectAndRegister();
    bool receiveLoop();
    bool sendMessage(const std::string &message);
    void enqueueEvent(const NetworkEvent &event);
    void setConnectionState(ConnectionState new_state, const std::string &message);

    std::string host_;
    std::string port_;
    std::string username_;
    std::string password_;

    std::atomic<bool> running_;
    std::atomic<bool> manual_disconnect_;
    std::thread worker_;

    mutable std::mutex connection_mutex_;
    ConnectionState connection_state_;
    std::intptr_t socket_handle_;

    std::mutex send_mutex_;
    std::mutex event_mutex_;
    std::condition_variable event_cv_;
    std::queue<NetworkEvent> events_;
};

#endif
