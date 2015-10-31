
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
        for (const auto &res : configuredResources.keys()) {
            //TODO filter by entity type
            resources << res;
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

KAsync::Job<void> Store::shutdown(const QByteArray &identifier)
{
    Trace() << "shutdown";
    return ResourceAccess::connectToServer(identifier).then<void, QSharedPointer<QLocalSocket>>([identifier](QSharedPointer<QLocalSocket> socket, KAsync::Future<void> &future) {
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
    })
    //FIXME JOBAPI this is only required because we don't care about the return value of connectToServer
    .template then<void>([](){});
}

KAsync::Job<void> Store::synchronize(const Akonadi2::Query &query)
{
    Trace() << "synchronize";
    return  KAsync::iterate(query.resources)
    .template each<void, QByteArray>([query](const QByteArray &resource, KAsync::Future<void> &future) {
        auto resourceAccess = QSharedPointer<Akonadi2::ResourceAccess>::create(resource);
        resourceAccess->open();
        resourceAccess->synchronizeResource(true, false).then<void>([&future]() {
            future.setFinished();
        }).exec();
    })
    //FIXME JOBAPI this is only required because we don't care about the return value of each (and each shouldn't even have a return value)
    .template then<void>([](){});
}

} // namespace Akonadi2
