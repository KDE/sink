/*
 *   Copyright (C) 2017 Christian Mollekopf <mollekopf@kolabsys.com>
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
#include <QCoreApplication>

#include "common/definitions.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"
#include "sink_version.h"

namespace SinkInfo
{

bool info(const QStringList &args, State &state)
{
    state.printLine(QString("Sink version: %1").arg(sink_VERSION_STRING));
    state.printLine(QString("Storage location: %1").arg(Sink::storageLocation()));
    state.printLine(QString("Data location: %1").arg(Sink::dataLocation()));
    state.printLine(QString("Config location: %1").arg(Sink::configLocation()));
    state.printLine(QString("Temporary file location: %1").arg(Sink::temporaryFileLocation()));
    state.printLine(QString("Resource storage location: %1").arg(Sink::resourceStorageLocation("$RESOURCE")));
    state.printLine(QString("Resource lookup directories: %1").arg(QCoreApplication::instance()->libraryPaths().join(", ")));
    return false;
}

Syntax::List syntax()
{
    return Syntax::List() << Syntax{"info", QObject::tr("Shows general system info"), &SinkInfo::info, Syntax::NotInteractive};
}

REGISTER_SYNTAX(SinkInfo)

}
