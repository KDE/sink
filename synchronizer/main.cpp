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
#include <cxxabi.h>
#include <dlfcn.h>
#include <ostream>
#include <sstream>

#include "listener.h"
#include "log.h"
#include "test.h"

#undef DEBUG_AREA
#define DEBUG_AREA "resource"

static Listener *listener = nullptr;

//Print a demangled stacktrace
void printStacktrace()
{
    int skip = 1;
	void *callstack[128];
	const int nMaxFrames = sizeof(callstack) / sizeof(callstack[0]);
	char buf[1024];
	int nFrames = backtrace(callstack, nMaxFrames);
	char **symbols = backtrace_symbols(callstack, nFrames);

	std::ostringstream trace_buf;
	for (int i = skip; i < nFrames; i++) {
		// printf("%s\n", symbols[i]);
		Dl_info info;
		if (dladdr(callstack[i], &info) && info.dli_sname) {
			char *demangled = NULL;
			int status = -1;
			if (info.dli_sname[0] == '_') {
				demangled = abi::__cxa_demangle(info.dli_sname, NULL, 0, &status);
            }
			snprintf(buf, sizeof(buf), "%-3d %*p %s + %zd\n",
					 i, int(2 + sizeof(void*) * 2), callstack[i],
					 status == 0 ? demangled :
					 info.dli_sname == 0 ? symbols[i] : info.dli_sname,
					 (char *)callstack[i] - (char *)info.dli_saddr);
			free(demangled);
		} else {
			snprintf(buf, sizeof(buf), "%-3d %*p %s\n",
					 i, int(2 + sizeof(void*) * 2), callstack[i], symbols[i]);
		}
		trace_buf << buf;
	}
	free(symbols);
	if (nFrames == nMaxFrames) {
		trace_buf << "[truncated]\n";
    }
    std::cerr << trace_buf.str();
}

static int sCounter = 0;

void crashHandler(int signal)
{
    //Guard against crashing in here
    if (sCounter > 1) {
        std::_Exit(EXIT_FAILURE);
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
    app.setQuitLockEnabled(false);

    QByteArrayList arguments;
    for (int i = 0; i < argc; i++) {
        arguments << argv[i];
    }
    if (arguments.contains("--test")) {
        Log() << "Running in test-mode";
        arguments.removeAll("--test");
        Sink::Test::setTestModeEnabled(true);
    }

    if (arguments.count() < 3) {
        Warning() << "Not enough args passed, no resource loaded.";
        return app.exec();
    }

    const QByteArray instanceIdentifier = arguments.at(1);
    const QByteArray resourceType = arguments.at(2);
    app.setApplicationName(instanceIdentifier);
    Sink::Log::setPrimaryComponent(instanceIdentifier);
    Log() << "Starting: " << instanceIdentifier;

    QLockFile lockfile(instanceIdentifier + ".lock");
    lockfile.setStaleLockTime(500);
    if (!lockfile.tryLock(0)) {
        Warning() << "Failed to acquire exclusive lock on socket.";
        return -1;
    }

    listener = new Listener(instanceIdentifier, resourceType, &app);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, listener, &Listener::closeAllConnections);
    QObject::connect(listener, &Listener::noClients, &app, &QCoreApplication::quit);

    auto ret = app.exec();
    Log() << "Exiting: " << instanceIdentifier;
    return ret;
}
