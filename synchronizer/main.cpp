
#include <QApplication>

#include "common/console.h"
#include "listener.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    if (argc < 2) {
        new Akonadi2::Console(QString("Resource: ???"));
        Akonadi2::Console::main()->log("Not enough args passed, no resource loaded.");
        return app.exec();
    }

    new Akonadi2::Console(QString("Resource: %1").arg(argv[1]));
    Listener *listener = new Listener(argv[1]);

    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     listener, &Listener::closeAllConnections);
    QObject::connect(listener, &Listener::noClients,
                     &app, &QCoreApplication::quit);

    return app.exec();
}
