
#include "clientapi.h"
#include "resourceaccess.h"
#include "commands.h"
#include "log.h"
#include <QtConcurrent/QtConcurrentRun>

namespace async
{
    void run(const std::function<void()> &runner) {
        //TODO use a job that runs in a thread?
        QtConcurrent::run(runner);
    };
} // namespace async

namespace Akonadi2
{

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
