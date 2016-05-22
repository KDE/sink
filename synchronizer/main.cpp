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
#include <csignal>
#include <iostream>
#include <cstdlib>

#include "listener.h"
#include "log.h"

#undef DEBUG_AREA
#define DEBUG_AREA "resource"

Listener *listener = nullptr;

void printStacktrace()
{
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
}

int sCounter = 0;

void crashHandler(int signal)
{
    //Guard against crashing in here
    if (sCounter > 1) {
        std::_Exit(EXIT_FAILURE);
        return;
    }
    sCounter++;

    if (signal == SIGABRT) {
        std::cerr << "SIGABRT received\n";
    } else if (signal == SIGSEGV) {
        std::cerr << "SIGSEV received\n";
    } else {
        std::cerr << "Unexpected signal " << signal << " received\n";
    }

    printStacktrace();

    //Get the word out that we're going down
    listener->emergencyAbortAllConnections();

    // std::system("exec gdb -p \"$PPID\" -ex \"thread apply all bt\"");
    // This only works if we actually have xterm and X11 available
    // std::system("exec xterm -e gdb -p \"$PPID\"");

    std::_Exit(EXIT_FAILURE);
    return;
}

void terminateHandler()
{
    std::exception_ptr exptr = std::current_exception();
    if (exptr != 0)
    {
        // the only useful feature of std::exception_ptr is that it can be rethrown...
        try
        {
            std::rethrow_exception(exptr);
        }
        catch (std::exception &ex)
        {
            std::fprintf(stderr, "Terminated due to exception: %s\n", ex.what());
        }
        catch (...)
        {
            std::fprintf(stderr, "Terminated due to unknown exception\n");
        }
    }
    else
    {
        std::fprintf(stderr, "Terminated due to unknown reason :(\n");
    }
    std::abort();
}

int main(int argc, char *argv[])
{
    // For crashes
    std::signal(SIGSEGV, crashHandler);
    std::signal(SIGABRT, crashHandler);
    std::set_terminate(terminateHandler);

    QCoreApplication app(argc, argv);

    if (argc < 3) {
        Warning() << "Not enough args passed, no resource loaded.";
        return app.exec();
    }

    const QByteArray instanceIdentifier = argv[1];
    const QByteArray resourceType = argv[2];
    app.setApplicationName(instanceIdentifier);

    QLockFile lockfile(instanceIdentifier + ".lock");
    lockfile.setStaleLockTime(500);
    if (!lockfile.tryLock(0)) {
        Warning() << "Failed to acquire exclusive lock on socket.";
        return -1;
    }

    listener = new Listener(instanceIdentifier, resourceType, &app);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, listener, &Listener::closeAllConnections);
    QObject::connect(listener, &Listener::noClients, &app, &QCoreApplication::quit);

    return app.exec();
}
