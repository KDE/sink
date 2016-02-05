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

/**
 * Sets the debug output level.
 *
 * Everything below is ignored.
 */
void SINKCOMMON_EXPORT setDebugOutputLevel(DebugLevel);
DebugLevel SINKCOMMON_EXPORT debugOutputLevel();

/// Set debug areas that should be logged
void SINKCOMMON_EXPORT setAreas(const QByteArrayList &areas);

/**
 * Set an application name filter.
 *
 * Note: In case of resources the identifier is the application name.
 */
void SINKCOMMON_EXPORT setFilter(const QByteArrayList &filter);

/**
 * Set an application debug output.
 *
 * Currently supported are:
 * * Name: Application name used for filter.
 * * Function: The function name:
 * * Location: The source code location.
 */
void SINKCOMMON_EXPORT setDebugOutput(const QByteArrayList &filter);

QDebug SINKCOMMON_EXPORT debugStream(DebugLevel debugLevel, int line, const char* file, const char* function, const char* debugArea = 0);

}
}

#define DEBUG_AREA nullptr

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
