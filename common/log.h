#pragma once

#include "sinkcommon_export.h"
#include <QDebug>

namespace Sink {
namespace Log {

enum DebugLevel {
    Trace,
    Log,
    Warning,
    Error
};

QByteArray SINKCOMMON_EXPORT debugLevelName(DebugLevel debugLevel);
DebugLevel SINKCOMMON_EXPORT debugLevelFromName(const QByteArray &name);

void SINKCOMMON_EXPORT setDebugOutputLevel(DebugLevel);
DebugLevel SINKCOMMON_EXPORT debugOutputLevel();

QDebug SINKCOMMON_EXPORT debugStream(DebugLevel debugLevel, int line, const char* file, const char* function, const char* debugArea = 0);

}
}

#define DEBUG_AREA 0

#define Trace_() Sink::Log::debugStream(Sink::Log::DebugLevel::Trace, __LINE__, __FILE__, Q_FUNC_INFO)
#define Log_() Sink::Log::debugStream(Sink::Log::DebugLevel::Log, __LINE__, __FILE__, Q_FUNC_INFO)

#define Trace_area(AREA) Sink::Log::debugStream(Sink::Log::DebugLevel::Trace, __LINE__, __FILE__, Q_FUNC_INFO, AREA)
#define Log_area(AREA) Sink::Log::debugStream(Sink::Log::DebugLevel::Log, __LINE__, __FILE__, Q_FUNC_INFO, AREA)
#define Warning_area(AREA) Sink::Log::debugStream(Sink::Log::DebugLevel::Warning, __LINE__, __FILE__, Q_FUNC_INFO, AREA)
#define Error_area(AREA) Sink::Log::debugStream(Sink::Log::DebugLevel::Error, __LINE__, __FILE__, Q_FUNC_INFO, AREA)

#define Trace() Sink::Log::debugStream(Sink::Log::DebugLevel::Trace, __LINE__, __FILE__, Q_FUNC_INFO, DEBUG_AREA)
#define Log() Sink::Log::debugStream(Sink::Log::DebugLevel::Log, __LINE__, __FILE__, Q_FUNC_INFO, DEBUG_AREA)
#define Warning() Sink::Log::debugStream(Sink::Log::DebugLevel::Warning, __LINE__, __FILE__, Q_FUNC_INFO, DEBUG_AREA)
//FIXME Error clashes with Storage::Error and MessageQueue::Error
#define ErrorMsg() Sink::Log::debugStream(Sink::Log::DebugLevel::Error, __LINE__, __FILE__, Q_FUNC_INFO, DEBUG_AREA)
