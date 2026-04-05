#ifndef GUI_H
#define GUI_H

#include <unordered_map>

#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>

#include "network_thread.h"

class OperatorWindow : public QMainWindow {
public:
    OperatorWindow(
        NetworkThread &network,
        const QString &host,
        const QString &port,
        const QString &username,
        QWidget *parent = nullptr
    );
    ~OperatorWindow() override = default;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void wireEvents();
    void processNetworkEvents();
    void handleNetworkEvent(const NetworkEvent &event);
    void updateConnectionIndicator(ConnectionState state, const QString &message);
    void refreshSensorsTable();
    void applySensorResult(const std::vector<SensorSnapshot> &sensors);
    void applyMeasurementsResult(const std::vector<SensorSnapshot> &measurements);
    void prependAlert(const AlertSnapshot &alert);

    NetworkThread &network_;
    QString host_;
    QString port_;
    QString username_;

    QLabel *connection_indicator_;
    QLabel *connection_text_;
    QLabel *session_info_label_;
    QLabel *status_summary_label_;
    QTableWidget *sensors_table_;
    QListWidget *alerts_list_;
    QPushButton *query_sensors_button_;
    QPushButton *query_measurements_button_;
    QPushButton *status_button_;
    QPushButton *disconnect_button_;
    QTimer *poll_timer_;

    std::unordered_map<std::string, SensorSnapshot> sensors_;
};

#endif
