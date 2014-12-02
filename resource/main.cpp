
#include <QApplication>

#include "common/console.h"
#include "listener.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    new Console(QString("Resource: %1").arg(argv[1]));
    if (argc < 2) {
        Console::main()->log("Not enough args");
        return app.exec();
    }

    Listener *listener = new Listener(argv[1]);

    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     listener, &Listener::closeAllConnections);
    QObject::connect(listener, &Listener::noClients,
                     &app, &QCoreApplication::quit);

    return app.exec();
}