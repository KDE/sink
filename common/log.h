#pragma once

#include <QDebug>

namespace Akonadi2 {
namespace Log {

enum DebugLevel {
    Trace,
    Log,
    Warning,
    Error
};

void setDebugOutputLevel(DebugLevel);

QDebug debugStream(DebugLevel debugLevel, int line, const char* file, const char* function, const char* debugArea = 0);

}
}

#define Trace() Akonadi2::Log::debugStream(Akonadi2::Log::DebugLevel::Trace, __LINE__, __FILE__, Q_FUNC_INFO)
#define Log() Akonadi2::Log::debugStream(Akonadi2::Log::DebugLevel::Log, __LINE__, __FILE__, Q_FUNC_INFO)
#define Warning() Akonadi2::Log::debugStream(Akonadi2::Log::DebugLevel::Warning, __LINE__, __FILE__, Q_FUNC_INFO)
//FIXME Error clashes with Storage::Error and MessageQueue::Error
#define ErrorMsg() Akonadi2::Log::debugStream(Akonadi2::Log::DebugLevel::Error, __LINE__, __FILE__, Q_FUNC_INFO)
