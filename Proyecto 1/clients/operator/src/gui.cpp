#include "gui.h"

#include <QCloseEvent>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QString connectionColor(ConnectionState state) {
    switch (state) {
        case ConnectionState::Connected:
            return "#2e9d5a";
        case ConnectionState::Connecting:
        case ConnectionState::Reconnecting:
            return "#d8a31a";
        case ConnectionState::Disconnected:
        default:
            return "#c84444";
    }
}

QString connectionLabel(ConnectionState state) {
    switch (state) {
        case ConnectionState::Connected:
            return "Conectado";
        case ConnectionState::Connecting:
            return "Conectando";
        case ConnectionState::Reconnecting:
            return "Reconectando";
        case ConnectionState::Disconnected:
        default:
            return "Desconectado";
    }
}

}  // namespace

OperatorWindow::OperatorWindow(
    NetworkThread &network,
    const QString &host,
    const QString &port,
    const QString &username,
    QWidget *parent
) :
    QMainWindow(parent),
    network_(network),
    host_(host),
    port_(port),
    username_(username),
    connection_indicator_(nullptr),
    connection_text_(nullptr),
    session_info_label_(nullptr),
    status_summary_label_(nullptr),
    sensors_table_(nullptr),
    alerts_list_(nullptr),
    query_sensors_button_(nullptr),
    query_measurements_button_(nullptr),
    status_button_(nullptr),
    disconnect_button_(nullptr),
    poll_timer_(nullptr) {
    buildUi();
    wireEvents();
    updateConnectionIndicator(ConnectionState::Connecting, "Inicializando conexion...");
}

void OperatorWindow::closeEvent(QCloseEvent *event) {
    network_.disconnect();
    event->accept();
}

void OperatorWindow::buildUi() {
    auto *central = new QWidget(this);
    auto *root_layout = new QVBoxLayout(central);
    auto *button_layout = new QHBoxLayout();
    auto *content_layout = new QHBoxLayout();

    auto *indicator_frame = new QFrame(central);
    indicator_frame->setFrameShape(QFrame::StyledPanel);
    auto *indicator_layout = new QHBoxLayout(indicator_frame);

    connection_indicator_ = new QLabel(indicator_frame);
    connection_indicator_->setFixedSize(18, 18);

    connection_text_ = new QLabel("Desconectado", indicator_frame);
    session_info_label_ = new QLabel(
        QString("Host: %1  Puerto: %2  Usuario: %3").arg(host_, port_, username_),
        indicator_frame
    );
    status_summary_label_ = new QLabel("Estado del servidor: sin datos recientes", indicator_frame);

    indicator_layout->addWidget(connection_indicator_);
    indicator_layout->addWidget(connection_text_);
    indicator_layout->addSpacing(18);
    indicator_layout->addWidget(session_info_label_, 1);
    indicator_layout->addWidget(status_summary_label_, 1);

    query_sensors_button_ = new QPushButton("Consultar Sensores", central);
    query_measurements_button_ = new QPushButton("Consultar Mediciones", central);
    status_button_ = new QPushButton("Estado del Servidor", central);
    disconnect_button_ = new QPushButton("Desconectar", central);

    button_layout->addWidget(query_sensors_button_);
    button_layout->addWidget(query_measurements_button_);
    button_layout->addWidget(status_button_);
    button_layout->addStretch(1);
    button_layout->addWidget(disconnect_button_);

    sensors_table_ = new QTableWidget(0, 4, central);
    sensors_table_->setHorizontalHeaderLabels(
        QStringList() << "ID" << "Tipo" << "Ultima medicion" << "Timestamp"
    );
    sensors_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    sensors_table_->verticalHeader()->setVisible(false);
    sensors_table_->setSelectionMode(QAbstractItemView::NoSelection);
    sensors_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    alerts_list_ = new QListWidget(central);
    alerts_list_->setAlternatingRowColors(true);

    auto *left_panel = new QWidget(central);
    auto *left_layout = new QVBoxLayout(left_panel);
    left_layout->addWidget(new QLabel("Sensores activos", left_panel));
    left_layout->addWidget(sensors_table_, 1);

    auto *right_panel = new QWidget(central);
    auto *right_layout = new QVBoxLayout(right_panel);
    right_layout->addWidget(new QLabel("Alertas recientes", right_panel));
    right_layout->addWidget(alerts_list_, 1);

    content_layout->addWidget(left_panel, 3);
    content_layout->addWidget(right_panel, 2);

    root_layout->addWidget(indicator_frame);
    root_layout->addLayout(button_layout);
    root_layout->addLayout(content_layout, 1);

    setCentralWidget(central);
    statusBar()->showMessage("Cliente operador listo");
}

void OperatorWindow::wireEvents() {
    connect(query_sensors_button_, &QPushButton::clicked, this, [this]() {
        network_.sendQuery("SENSORS");
        statusBar()->showMessage("Consulta de sensores enviada");
    });

    connect(query_measurements_button_, &QPushButton::clicked, this, [this]() {
        network_.sendQuery("MEASUREMENTS");
        statusBar()->showMessage("Consulta de mediciones enviada");
    });

    connect(status_button_, &QPushButton::clicked, this, [this]() {
        network_.sendStatus();
        statusBar()->showMessage("Consulta de estado enviada");
    });

    connect(disconnect_button_, &QPushButton::clicked, this, [this]() {
        network_.disconnect();
        statusBar()->showMessage("Desconexion solicitada");
    });

    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(200);
    connect(poll_timer_, &QTimer::timeout, this, [this]() {
        processNetworkEvents();
    });
    poll_timer_->start();
}

void OperatorWindow::processNetworkEvents() {
    const std::vector<NetworkEvent> events = network_.takePendingEvents();

    for (const NetworkEvent &event : events) {
        handleNetworkEvent(event);
    }
}

void OperatorWindow::handleNetworkEvent(const NetworkEvent &event) {
    switch (event.type) {
        case NetworkEventType::ConnectionStateChanged:
            updateConnectionIndicator(event.connection_state, QString::fromStdString(event.message));
            break;
        case NetworkEventType::SensorsUpdated:
            applySensorResult(event.sensors);
            statusBar()->showMessage("Tabla de sensores actualizada");
            break;
        case NetworkEventType::MeasurementsUpdated:
            applyMeasurementsResult(event.measurements);
            statusBar()->showMessage("Ultimas mediciones actualizadas");
            break;
        case NetworkEventType::AlertReceived:
            prependAlert(event.alert);
            statusBar()->showMessage("Nueva alerta recibida");
            break;
        case NetworkEventType::StatusUpdated:
            status_summary_label_->setText(
                QString("Estado del servidor: sensores=%1 operadores=%2 uptime=%3s")
                    .arg(QString::fromStdString(event.status.active_sensors))
                    .arg(QString::fromStdString(event.status.active_operators))
                    .arg(QString::fromStdString(event.status.uptime_seconds))
            );
            statusBar()->showMessage("Estado del servidor actualizado");
            break;
        case NetworkEventType::ErrorMessage:
            statusBar()->showMessage(QString::fromStdString(event.message), 8000);
            break;
        case NetworkEventType::InfoMessage:
        default:
            if (!event.message.empty()) {
                statusBar()->showMessage(QString::fromStdString(event.message), 4000);
            }
            break;
    }
}

void OperatorWindow::updateConnectionIndicator(ConnectionState state, const QString &message) {
    connection_indicator_->setStyleSheet(
        QString("background-color: %1; border-radius: 9px;").arg(connectionColor(state))
    );
    connection_text_->setText(connectionLabel(state));
    if (!message.isEmpty()) {
        statusBar()->showMessage(message, 4000);
    }
}

void OperatorWindow::refreshSensorsTable() {
    sensors_table_->setRowCount(static_cast<int>(sensors_.size()));

    int row = 0;
    for (const auto &entry : sensors_) {
        const SensorSnapshot &sensor = entry.second;
        sensors_table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(sensor.sensor_id)));
        sensors_table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(sensor.type)));
        sensors_table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(sensor.value)));
        sensors_table_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(sensor.timestamp)));
        ++row;
    }
}

void OperatorWindow::applySensorResult(const std::vector<SensorSnapshot> &sensors) {
    sensors_.clear();
    for (const SensorSnapshot &sensor : sensors) {
        sensors_[sensor.sensor_id] = sensor;
    }
    refreshSensorsTable();
}

void OperatorWindow::applyMeasurementsResult(const std::vector<SensorSnapshot> &measurements) {
    for (const SensorSnapshot &measurement : measurements) {
        auto it = sensors_.find(measurement.sensor_id);
        if (it == sensors_.end()) {
            sensors_[measurement.sensor_id] = measurement;
        } else {
            it->second.type = measurement.type;
            it->second.value = measurement.value;
            it->second.timestamp = measurement.timestamp;
        }
    }
    refreshSensorsTable();
}

void OperatorWindow::prependAlert(const AlertSnapshot &alert) {
    const QString text = QString("[%1] %2 (%3) valor=%4 umbral=%5")
        .arg(QString::fromStdString(alert.timestamp))
        .arg(QString::fromStdString(alert.sensor_id))
        .arg(QString::fromStdString(alert.type))
        .arg(QString::fromStdString(alert.value))
        .arg(QString::fromStdString(alert.threshold));

    alerts_list_->insertItem(0, text);
    while (alerts_list_->count() > 200) {
        delete alerts_list_->takeItem(alerts_list_->count() - 1);
    }
}
