
#include "clientapi.h"
#include "resourceaccess.h"
#include "commands.h"

namespace async
{
    void run(const std::function<void()> &runner) {
        QtConcurrent::run(runner);

        // //FIXME we should be using a Job instead of a timer
        // auto timer = new QTimer;
        // timer->setSingleShot(true);
        // QObject::connect(timer, &QTimer::timeout, runner);
        // QObject::connect(timer, &QTimer::timeout, timer, &QObject::deleteLater);
        // timer->start(0);
    };
} // namespace async

namespace Akonadi2
{

namespace Domain
{

template<>
QString getTypeName<Event>()
{
    return "event";
}

template<>
QString getTypeName<Todo>()
{
    return "todo";
}

} // namespace Domain

void Store::shutdown(const QString &identifier)
{
    Akonadi2::ResourceAccess resourceAccess(identifier);
    resourceAccess.open();
    resourceAccess.sendCommand(Akonadi2::Commands::ShutdownCommand).then<void>([](Async::Future<void> &f){
        //TODO wait for disconnect
        f.setFinished();
    }).exec().waitForFinished();
}

} // namespace Akonadi2
