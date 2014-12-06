
#include <QApplication>
#include <QCommandLineParser>

#include "common/console.h"
#include "resourceaccess.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    new Console("Akonadi2 Client");

    ResourceAccess *resAccess = 0;
    QCommandLineParser cliOptions;
    cliOptions.addPositionalArgument(QObject::tr("[resource]"),
                                     QObject::tr("A resource to connect to"));
    cliOptions.process(app);
    QStringList resources = cliOptions.positionalArguments();
    if (resources.isEmpty()) {
        resources << "toy";
    }

    for (const QString &resource: resources) {
        resAccess = new ResourceAccess(resource);
        QObject::connect(&app, &QCoreApplication::aboutToQuit,
                        resAccess, &ResourceAccess::close);
        resAccess->open();
    }

    return app.exec();
}
