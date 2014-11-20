
#include <QApplication>

#include "common/console.h"
#include "resourceaccess.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    new Console("Toy Client");
    ResourceAccess *resAccess = new ResourceAccess("toyResource");

    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     resAccess, &ResourceAccess::close);

    resAccess->open();
    return app.exec();
}