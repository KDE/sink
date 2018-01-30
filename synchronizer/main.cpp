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
#include <QDir>

#include <signal.h>
#include <execinfo.h>
#include <csignal>
#include <iostream>
#include <cstdlib>
#include <cxxabi.h>
#include <dlfcn.h>
#include <ostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <unistd.h>

#include "listener.h"
#include "log.h"
#include "test.h"
#include "definitions.h"

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

    std::fprintf(stdout, "Sleeping for 10s to attach a debugger: gdb attach %i\n", getpid());
    std::this_thread::sleep_for(std::chrono::seconds(10));

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
        try {
            std::rethrow_exception(exptr);
        } catch (std::exception &ex) {
            std::fprintf(stderr, "Terminated due to exception: %s\n", ex.what());
        } catch (...) {
            std::fprintf(stderr, "Terminated due to unknown exception\n");
        }
    } else {
        std::fprintf(stderr, "Terminated due to unknown reason :(\n");
    }
    std::abort();
}

/*
 * We capture all qt debug messages in the same process and feed it into the sink debug system.
 * This way we get e.g. kimap debug messages as well together with the rest.
 */
void qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    switch (type) {
    case QtDebugMsg:
        Sink::Log::debugStream(Sink::Log::DebugLevel::Trace, context.line, context.file, context.function, context.category) << msg;
        break;
    case QtInfoMsg:
        Sink::Log::debugStream(Sink::Log::DebugLevel::Log, context.line, context.file, context.function, context.category) << msg;
        break;
    case QtWarningMsg:
        Sink::Log::debugStream(Sink::Log::DebugLevel::Warning, context.line, context.file, context.function, context.category) << msg;
        break;
    case QtCriticalMsg:
        Sink::Log::debugStream(Sink::Log::DebugLevel::Error, context.line, context.file, context.function, context.category) << msg;
        break;
    case QtFatalMsg:
        Sink::Log::debugStream(Sink::Log::DebugLevel::Error, context.line, context.file, context.function, context.category) << msg;
        abort();
    }
}


int main(int argc, char *argv[])
{
    const bool gdbDebugMode = qEnvironmentVariableIsSet("SINK_GDB_DEBUG");
    if (gdbDebugMode) {
        SinkWarning() << "Running resource in debug mode and waiting for gdb to attach: gdb attach " << getpid();
        raise(SIGSTOP);
    } else {
        // For crashes
        std::signal(SIGSEGV, crashHandler);
        std::signal(SIGABRT, crashHandler);
        std::set_terminate(terminateHandler);
    }

    qInstallMessageHandler(qtMessageHandler);

    QCoreApplication app(argc, argv);
    app.setQuitLockEnabled(false);

    QByteArrayList arguments;
    for (int i = 0; i < argc; i++) {
        arguments << argv[i];
    }
    if (arguments.contains("--test")) {
        SinkLog() << "Running in test-mode";
        arguments.removeAll("--test");
        Sink::Test::setTestModeEnabled(true);
    }

    if (arguments.count() < 3) {
        SinkWarning() << "Not enough args passed, no resource loaded.";
        return app.exec();
    }

    const QByteArray instanceIdentifier = arguments.at(1);
    const QByteArray resourceType = arguments.at(2);
    app.setApplicationName(instanceIdentifier);
    Sink::Log::setPrimaryComponent(instanceIdentifier);
    SinkLog() << "Starting: " << instanceIdentifier << resourceType;

    QDir{}.mkpath(Sink::resourceStorageLocation(instanceIdentifier));
    QLockFile lockfile(Sink::storageLocation() + QString("/%1.lock").arg(QString(instanceIdentifier)));
    lockfile.setStaleLockTime(500);
    if (!lockfile.tryLock(0)) {
        const auto error = lockfile.error();
        if (error == QLockFile::LockFailedError) {
            qint64 pid;
            QString hostname, appname;
            lockfile.getLockInfo(&pid, &hostname, &appname);
            SinkWarning() << "Failed to acquire exclusive resource lock.";
            SinkLog() << "Pid:" << pid << "Host:" << hostname << "App:" << appname;
        } else {
            SinkError() << "Error while trying to acquire exclusive resource lock: " << error;
        }
        return -1;
    }

    listener = new Listener(instanceIdentifier, resourceType, &app);
    listener->checkForUpgrade();

    QObject::connect(&app, &QCoreApplication::aboutToQuit, listener, &Listener::closeAllConnections);
    QObject::connect(listener, &Listener::noClients, &app, &QCoreApplication::quit);

    auto ret = app.exec();
    SinkLog() << "Exiting: " << instanceIdentifier;
    return ret;
}
