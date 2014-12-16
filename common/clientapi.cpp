
#include "clientapi.h"

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

} // namespace Akonadi2
