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
#include <QCommandLineParser>

#include "common/commands.h"
#include "common/console.h"
#include "common/resourceaccess.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    new Akonadi2::Console("Akonadi2 Client");

    QCommandLineParser cliOptions;
    cliOptions.addPositionalArgument(QObject::tr("[resource]"),
                                     QObject::tr("A resource to connect to"));
    cliOptions.process(app);
    QStringList resources = cliOptions.positionalArguments();
    if (resources.isEmpty()) {
        resources << "org.kde.dummy";
    }

    for (const QString &resource: resources) {
        Akonadi2::ResourceAccess *resAccess = new Akonadi2::ResourceAccess(resource);
        QObject::connect(&app, &QCoreApplication::aboutToQuit,
                        resAccess, &Akonadi2::ResourceAccess::close);
        resAccess->sendCommand(Akonadi2::Commands::SynchronizeCommand);
        resAccess->open();
    }

    return app.exec();
}
