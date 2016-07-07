#pragma once

#include "sink_export.h"
#include <QDebug>

namespace Sink {
namespace Log {

enum DebugLevel
{
    Trace,
    Log,
    Warning,
    Error
};

void SINK_EXPORT setPrimaryComponent(const QString &component);

QByteArray SINK_EXPORT debugLevelName(DebugLevel debugLevel);
DebugLevel SINK_EXPORT debugLevelFromName(const QByteArray &name);

/**
 * Sets the debug output level.
 *
 * Everything below is ignored.
 */
void SINK_EXPORT setDebugOutputLevel(DebugLevel);
DebugLevel SINK_EXPORT debugOutputLevel();

enum FilterType
{
    Area,
    ApplicationName
};

/**
 * Sets a debug output filter.
 *
 * Everything that is not matching the filter is ignored.
 * An empty filter matches everything.
 *
 * Note: In case of resources the application name is the identifier.
 */
void SINK_EXPORT setDebugOutputFilter(FilterType, const QByteArrayList &filter);
QByteArrayList SINK_EXPORT debugOutputFilter(FilterType type);

/**
 * Set the debug output fields.
 *
 * Currently supported are:
 * * Name: Application name used for filter.
 * * Function: The function name:
 * * Location: The source code location.
 *
 * These are additional items to the default ones (level, area, message).
 */
void SINK_EXPORT setDebugOutputFields(const QByteArrayList &filter);
QByteArrayList SINK_EXPORT debugOutputFields();

QDebug SINK_EXPORT debugStream(DebugLevel debugLevel, int line, const char *file, const char *function, const char *debugArea = 0, const char *debugComponent = 0);

struct SINK_EXPORT TraceTime
{
    TraceTime(int i) : time(i){};
    const int time;
};

inline QDebug SINK_EXPORT operator<<(QDebug d, const TraceTime &time)
{
    d << time.time << "[ms]";
    return d;
}
}
}

static const char *getComponentName() { return nullptr; }

#define Trace_area(AREA) Sink::Log::debugStream(Sink::Log::DebugLevel::Trace, __LINE__, __FILE__, Q_FUNC_INFO, AREA)
#define Log_area(AREA) Sink::Log::debugStream(Sink::Log::DebugLevel::Log, __LINE__, __FILE__, Q_FUNC_INFO, AREA)
#define Warning_area(AREA) Sink::Log::debugStream(Sink::Log::DebugLevel::Warning, __LINE__, __FILE__, Q_FUNC_INFO, AREA)
#define Error_area(AREA) Sink::Log::debugStream(Sink::Log::DebugLevel::Error, __LINE__, __FILE__, Q_FUNC_INFO, AREA)

#define SinkTrace_(COMPONENT, AREA) Sink::Log::debugStream(Sink::Log::DebugLevel::Trace, __LINE__, __FILE__, Q_FUNC_INFO, AREA, COMPONENT)
#define SinkLog_(COMPONENT, AREA) Sink::Log::debugStream(Sink::Log::DebugLevel::Log, __LINE__, __FILE__, Q_FUNC_INFO, AREA, COMPONENT)
#define SinkWarning_(COMPONENT, AREA) Sink::Log::debugStream(Sink::Log::DebugLevel::Warning, __LINE__, __FILE__, Q_FUNC_INFO, AREA, COMPONENT)
#define SinkError_(COMPONENT, AREA) Sink::Log::debugStream(Sink::Log::DebugLevel::Error, __LINE__, __FILE__, Q_FUNC_INFO, AREA, COMPONENT)

#define SinkTrace() Sink::Log::debugStream(Sink::Log::DebugLevel::Trace, __LINE__, __FILE__, Q_FUNC_INFO, s_sinkDebugArea, getComponentName())
#define SinkLog() Sink::Log::debugStream(Sink::Log::DebugLevel::Log, __LINE__, __FILE__, Q_FUNC_INFO, s_sinkDebugArea, getComponentName())
#define SinkWarning() Sink::Log::debugStream(Sink::Log::DebugLevel::Warning, __LINE__, __FILE__, Q_FUNC_INFO, s_sinkDebugArea, getComponentName())
#define SinkError() Sink::Log::debugStream(Sink::Log::DebugLevel::Error, __LINE__, __FILE__, Q_FUNC_INFO, s_sinkDebugArea, getComponentName())

#define SINK_DEBUG_AREA(AREA) static constexpr const char* s_sinkDebugArea{AREA};
#define SINK_DEBUG_COMPONENT(COMPONENT) const char* getComponentName() const { return COMPONENT; };
#define SINK_DEBUG_COMPONENT_STATIC(COMPONENT) static const char* getComponentName() { return COMPONENT; };
