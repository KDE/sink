
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

namespace ApplicationDomain
{

template<>
QByteArray getTypeName<Event>()
{
    return "event";
}

template<>
QByteArray getTypeName<Todo>()
{
    return "todo";
}

} // namespace Domain

void Store::shutdown(const QByteArray &identifier)
{
    Akonadi2::ResourceAccess resourceAccess(identifier);
    //FIXME this starts the resource, just to shut it down again if it's not running in the first place.
    resourceAccess.open();
    resourceAccess.sendCommand(Akonadi2::Commands::ShutdownCommand).exec().waitForFinished();
}

} // namespace Akonadi2
