/*
 *   Copyright (C) 2016 Christian Mollekopf <chrigi_1@fastmail.fm>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <QDebug>
#include <QObject> // tr()
#include <QTimer>

#include "common/resource.h"
#include "common/storage.h"
#include "common/resourceconfig.h"
#include "common/log.h"
#include "common/storage.h"
#include "common/definitions.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"
#include "iostream"

namespace SinkTrace
{

bool traceOff(const QStringList &args, State &state)
{
    Sink::Log::setDebugOutputLevel(Sink::Log::Log);
    std::cout << "Turned trace off." << std::endl;
    return true;
}

bool traceOn(const QStringList &args, State &state)
{
    Sink::Log::setDebugOutputLevel(Sink::Log::Trace);
    if (args.isEmpty() || (args.size() == 1 && args.first() == "*")) {
        Sink::Log::setDebugOutputFilter(Sink::Log::Area, QByteArrayList());
        std::cout << "Set trace filter to: *" << std::endl;
    } else {
        QByteArrayList filter;
        for (const auto &arg : args) {
            filter << arg.toLatin1();
        }
        Sink::Log::setDebugOutputFilter(Sink::Log::Area, filter);
        std::cout << "Set trace filter to: " << filter.join(", ").toStdString() << std::endl;
    }
    return true;
}

bool trace(const QStringList &args, State &state)
{
    return traceOn(args, state);
}


Syntax::List syntax()
{
    Syntax trace("trace", QObject::tr("Control trace debug output."), &SinkTrace::trace, Syntax::NotInteractive);
    trace.completer = &SinkshUtils::debugareaCompleter; 

    Syntax traceOff("off", QObject::tr("Turns off trace output."), &SinkTrace::traceOff, Syntax::NotInteractive);
    traceOff.completer = &SinkshUtils::debugareaCompleter; 
    trace.children << traceOff;

    Syntax traceOn("on", QObject::tr("Turns on trace output."), &SinkTrace::traceOn, Syntax::NotInteractive);
    traceOn.completer = &SinkshUtils::debugareaCompleter; 
    trace.children << traceOn;

    return Syntax::List() << trace;
}

REGISTER_SYNTAX(SinkTrace)

}
