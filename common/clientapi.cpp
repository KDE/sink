
#include "clientapi.h"
#include "resourceaccess.h"
#include "commands.h"

namespace async
{
    void run(const std::function<void()> &runner) {
        //TODO use a job that runs in a thread?
        QtConcurrent::run(runner);
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
    //FIXME this starts the resource, just to shut it down again if it's not running in the first place.
    resourceAccess.open();
    resourceAccess.sendCommand(Akonadi2::Commands::ShutdownCommand).then<void>([](Async::Future<void> &f){
        //TODO wait for disconnect
        f.setFinished();
    }).exec().waitForFinished();
}

} // namespace Akonadi2
