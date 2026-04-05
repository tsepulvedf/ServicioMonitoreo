#include <QString>
#include <QApplication>

#include "gui.h"
#include "network_thread.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QString host = "localhost";
    QString port = "9000";
    QString username = "operator1";
    QString password = "op123";

    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = argv[2];
    }
    if (argc > 3) {
        username = argv[3];
    }
    if (argc > 4) {
        password = argv[4];
    }

    NetworkThread network(
        host.toStdString(),
        port.toStdString(),
        username.toStdString(),
        password.toStdString()
    );
    network.start();

    OperatorWindow window(network, host, port, username);
    window.resize(1100, 720);
    window.show();

    return app.exec();
}
