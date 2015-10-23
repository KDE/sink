
#include "clientapi.h"
#include "resourceaccess.h"
#include "commands.h"
#include "resourcefacade.h"
#include "log.h"
#include "definitions.h"
#include "resourceconfig.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>

#define ASYNCINTHREAD

namespace async
{
    void run(const std::function<void()> &runner) {
        auto timer = new QTimer();
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, [runner, timer]() {
            delete timer;
#ifndef ASYNCINTHREAD
            runner();
#else
            QtConcurrent::run(runner);
#endif
        });
        timer->start(0);
    };
} // namespace async


namespace Akonadi2
{

QString Store::storageLocation()
{
    return Akonadi2::storageLocation();
}

QByteArray Store::resourceName(const QByteArray &instanceIdentifier)
{
    return Akonadi2::resourceName(instanceIdentifier);
}

QList<QByteArray> Store::getResources(const QList<QByteArray> &resourceFilter, const QByteArray &type)
{
    //Return the global resource (signified by an empty name) for types that don't eblong to a specific resource
    if (type == "akonadiresource") {
        qWarning() << "Global resource";
        return QList<QByteArray>() << "";
    }
    QList<QByteArray> resources;
    const auto configuredResources = ResourceConfig::getResources();
    if (resourceFilter.isEmpty()) {
        for (const auto &res : configuredResources) {
            if (configuredResources.value(res) == type) {
                resources << res;
            }
        }
    } else {
        for (const auto &res : resourceFilter) {
            if (configuredResources.contains(res)) {
                resources << res;
            } else {
                qWarning() << "Resource is not existing: " << res;
            }
        }
    }
    qWarning() << "Found resources: " << resources;
    return resources;
}

void Store::shutdown(const QByteArray &identifier)
{
    Trace() << "shutdown";
    ResourceAccess::connectToServer(identifier).then<void, QSharedPointer<QLocalSocket>>([identifier](const QSharedPointer<QLocalSocket> &socket, KAsync::Future<void> &future) {
        //We can't currently reuse the socket
        socket->close();
        auto resourceAccess = QSharedPointer<Akonadi2::ResourceAccess>::create(identifier);
        resourceAccess->open();
        resourceAccess->sendCommand(Akonadi2::Commands::ShutdownCommand).then<void>([&future, resourceAccess]() {
            future.setFinished();
        }).exec();
    },
    [](int, const QString &) {
        //Resource isn't started, nothing to shutdown
    }).exec().waitForFinished();
}

void Store::synchronize(const QByteArray &identifier)
{
    Trace() << "synchronize";
    auto resourceAccess = QSharedPointer<Akonadi2::ResourceAccess>::create(identifier);
    resourceAccess->open();
    resourceAccess->synchronizeResource(true, false).exec().waitForFinished();
}

} // namespace Akonadi2
