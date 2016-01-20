#pragma once

#include <QDebug>

namespace Sink {
namespace Log {

enum DebugLevel {
    Trace,
    Log,
    Warning,
    Error
};

QByteArray debugLevelName(DebugLevel debugLevel);
DebugLevel debugLevelFromName(const QByteArray &name);

void setDebugOutputLevel(DebugLevel);
DebugLevel debugOutputLevel();

QDebug debugStream(DebugLevel debugLevel, int line, const char* file, const char* function, const char* debugArea = 0);

}
}

#define Trace() Sink::Log::debugStream(Sink::Log::DebugLevel::Trace, __LINE__, __FILE__, Q_FUNC_INFO)
#define Log() Sink::Log::debugStream(Sink::Log::DebugLevel::Log, __LINE__, __FILE__, Q_FUNC_INFO)
#define Warning() Sink::Log::debugStream(Sink::Log::DebugLevel::Warning, __LINE__, __FILE__, Q_FUNC_INFO)
//FIXME Error clashes with Storage::Error and MessageQueue::Error
#define ErrorMsg() Sink::Log::debugStream(Sink::Log::DebugLevel::Error, __LINE__, __FILE__, Q_FUNC_INFO)
