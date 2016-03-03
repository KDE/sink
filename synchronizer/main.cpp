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

#include <QCoreApplication>
#include <QLockFile>

#include <signal.h>
#include <execinfo.h>

#include "listener.h"
#include "log.h"

#undef DEBUG_AREA
#define DEBUG_AREA "resource"

void crashHandler(int sig)
{
    std::fprintf(stderr, "Error: signal %d\n", sig);

    QString s;
    void *trace[256];
    int n = backtrace(trace, 256);
    if (n) {
        char **strings = backtrace_symbols(trace, n);

        s = QLatin1String("[\n");

        for (int i = 0; i < n; ++i) {
            s += QString::number(i) + QLatin1String(": ") + QLatin1String(strings[i]) + QLatin1String("\n");
        }
        s += QLatin1String("]\n");
        std::fprintf(stderr, "Backtrace: %s\n", s.toLatin1().data());

        if (strings) {
            free(strings);
        }
    }

    std::system("exec gdb -p \"$PPID\" -ex \"thread apply all bt\"");
    // This only works if we actually have xterm and X11 available
    // std::system("exec xterm -e gdb -p \"$PPID\"");

    std::abort();
}

int main(int argc, char *argv[])
{
    // For crashes
    signal(SIGSEGV, crashHandler);
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        Warning() << "Not enough args passed, no resource loaded.";
        return app.exec();
    }

    const QByteArray instanceIdentifier = argv[1];
    app.setApplicationName(instanceIdentifier);

    QLockFile lockfile(instanceIdentifier + ".lock");
    lockfile.setStaleLockTime(500);
    if (!lockfile.tryLock(0)) {
        Warning() << "Failed to acquire exclusive lock on socket.";
        return -1;
    }

    Listener *listener = new Listener(instanceIdentifier, &app);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, listener, &Listener::closeAllConnections);
    QObject::connect(listener, &Listener::noClients, &app, &QCoreApplication::quit);

    return app.exec();
}
