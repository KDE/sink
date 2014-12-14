#include "threadboundary.h"

Q_DECLARE_METATYPE(std::function<void()>);

namespace async {
ThreadBoundary::ThreadBoundary(): QObject() { qRegisterMetaType<std::function<void()> >("std::function<void()>"); }
ThreadBoundary:: ~ThreadBoundary() {}
}

