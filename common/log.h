#pragma once

#include <QDebug>

enum DebugLevel {
    Trace,
    Log,
    Warning,
    Error
};

QDebug debugStream(DebugLevel debugLevel, int line, const char* file, const char* function, const char* debugArea = 0);

#define Trace() debugStream(DebugLevel::Trace, __LINE__, __FILE__, Q_FUNC_INFO)
#define Log() debugStream(DebugLevel::Log, __LINE__, __FILE__, Q_FUNC_INFO)
#define Warning() debugStream(DebugLevel::Warning, __LINE__, __FILE__, Q_FUNC_INFO)
//FIXME Error clashes with Storage::Error and MessageQueue::Error
#define ErrorMsg() debugStream(DebugLevel::Error, __LINE__, __FILE__, Q_FUNC_INFO)
