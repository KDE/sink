/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
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

#include <QApplication>

#include "common/console.h"
#include "listener.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    if (argc < 2) {
        new Akonadi2::Console(QString("Resource: ???"));
        Akonadi2::Console::main()->log("Not enough args passed, no resource loaded.");
        return app.exec();
    }

    new Akonadi2::Console(QString("Resource: %1").arg(argv[1]));
    Listener *listener = new Listener(argv[1]);

    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     listener, &Listener::closeAllConnections);
    QObject::connect(listener, &Listener::noClients,
                     &app, &QCoreApplication::quit);

    return app.exec();
}
