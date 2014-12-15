
#include <QApplication>
#include <QCommandLineParser>

#include "common/console.h"
#include "resourceaccess.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    new Akonadi2::Console("Akonadi2 Client");

    QCommandLineParser cliOptions;
    cliOptions.addPositionalArgument(QObject::tr("[resource]"),
                                     QObject::tr("A resource to connect to"));
    cliOptions.process(app);
    QStringList resources = cliOptions.positionalArguments();
    if (resources.isEmpty()) {
        resources << "toy";
    }

    for (const QString &resource: resources) {
        Akonadi2::ResourceAccess *resAccess = new Akonadi2::ResourceAccess(resource);
        QObject::connect(&app, &QCoreApplication::aboutToQuit,
                        resAccess, &Akonadi2::ResourceAccess::close);
        resAccess->open();
    }

    return app.exec();
}
