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
#include <QTimer>

#include "common/store.h"

#include "state.h"
#include "syntaxtree.h"

namespace SinkUpgrade
{

bool upgrade(const QStringList &args, State &state)
{
    state.print(QObject::tr("Upgrading..."));
    Sink::Store::upgrade().exec().waitForFinished();
    state.printLine(QObject::tr("done"));
    return true;
}

Syntax::List syntax()
{
    return Syntax::List() << Syntax{"upgrade", QObject::tr("Upgrades your storage to the latest version (be careful!)"), &SinkUpgrade::upgrade, Syntax::NotInteractive};
}

REGISTER_SYNTAX(SinkUpgrade)

}
