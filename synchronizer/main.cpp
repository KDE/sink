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

#include <QGuiApplication>
#include <QLockFile>
#include <QDir>
#include <QTime>

#include <signal.h>
#ifndef Q_OS_WIN
#include <unistd.h>
#else
#include <io.h>
#include <process.h>
#endif

#include "listener.h"
#include "log.h"
#include "test.h"
#include "definitions.h"
#include "backtrace.h"
#ifdef Q_OS_OSX
#include <CoreFoundation/CoreFoundation.h>
#endif




/*
 * We capture all qt debug messages in the same process and feed it into the sink debug system.
 * This way we get e.g. kimap debug messages as well together with the rest.
 */
static void qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
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

static QString read(const QString &filename)
{
    QFile file{filename};
    file.open(QIODevice::ReadOnly);
    return file.readAll();
}

void printStats()
{

#if defined(Q_OS_LINUX)
    /*
     * See 'man proc' for details
     */
    {
        auto statm = read("/proc/self/statm").split(' ');
        SinkLog() << "Program size:" << statm.value(0).toInt() << "pages";
        SinkLog() << "RSS:"<< statm.value(1).toInt() << "pages";
        SinkLog() << "Resident Shared:" << statm.value(2).toInt() << "pages";
        SinkLog() << "Text (code):" << statm.value(3).toInt() << "pages";
        SinkLog() << "Data (data + stack):" << statm.value(5).toInt() << "pages";
    }

    {
        auto stat = read("/proc/self/stat").split(' ');
        SinkLog() << "Minor page faults: " << stat.value(10).toInt();
        SinkLog() << "Children minor page faults: " << stat.value(11).toInt();
        SinkLog() << "Major page faults: " << stat.value(12).toInt();
        SinkLog() << "Children major page faults: " << stat.value(13).toInt();
    }

    //Dump the complete memory map for the process
    // std::cout << "smaps: " << read("/proc/self/smaps").toStdString();
    //Dump all sorts of stats for the process
    // std::cout << read("/proc/self/status").toStdString();

    {
        auto io = read("/proc/self/io").split('\n');
        QHash<QString, QString> hash;
        for (const auto &s : io) {
            const auto parts = s.split(": ");
            hash.insert(parts.value(0), parts.value(1));
        }
        SinkLog() << "Read syscalls: " << hash.value("syscr").toInt();
        SinkLog() << "Write syscalls: " << hash.value("syscw").toInt();
        SinkLog() << "Read from disk: " << hash.value("read_bytes").toInt() / 1024 << "kb";
        SinkLog() << "Written to disk: " << hash.value("write_bytes").toInt() / 1024 << "kb";
        SinkLog() << "Cancelled write bytes: " << hash.value("cancelled_write_bytes").toInt();
    }

#endif
}

class SynchronizerApplication : public QGuiApplication
{
    Q_OBJECT
protected:
    using QGuiApplication::QGuiApplication;

    QTime time;

    /*
     * If we block the event loop for too long the system becomes unresponsive to user inputs,
     * so we monitor it and attempt to avoid blocking behaviour
     */
    bool notify(QObject *receiver, QEvent *event) override
    {
        time.start();
        const auto ret = QGuiApplication::notify(receiver, event);
        if (time.elapsed() > 1000) {
            SinkWarning() << "Blocked the eventloop for " << Sink::Log::TraceTime(time.elapsed()) << " with event " << event->type();
        }
        return ret;
    }
};

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsSet("SINK_GDB_DEBUG")) {
#ifndef Q_OS_WIN
        SinkWarning() << "Running resource in debug mode and waiting for gdb to attach: gdb attach " << getpid();
        raise(SIGSTOP);
#endif
    } else {
        Sink::installCrashHandler();
    }

    qInstallMessageHandler(qtMessageHandler);

#ifdef Q_OS_OSX
    //Necessary to hide this QGuiApplication from the dock and application switcher on mac os.
    if (CFBundleRef mainBundle = CFBundleGetMainBundle()) {
        // get the application's Info Dictionary. For app bundles this would live in the bundle's Info.plist,
        if (CFMutableDictionaryRef infoDict = (CFMutableDictionaryRef) CFBundleGetInfoDictionary(mainBundle)) {
            // Add or set the "LSUIElement" key with/to value "1". This can simply be a CFString.
            CFDictionarySetValue(infoDict, CFSTR("LSUIElement"), CFSTR("1"));
            // That's it. We're now considered as an "agent" by the window server, and thus will have
            // neither menubar nor presence in the Dock or App Switcher.
        }
    }
#endif

    SynchronizerApplication app(argc, argv);
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

    auto listener = new Listener(instanceIdentifier, resourceType, &app);
    Sink::setListener(listener);
    listener->checkForUpgrade();

    QObject::connect(&app, &QCoreApplication::aboutToQuit, listener, &Listener::closeAllConnections);
    QObject::connect(listener, &Listener::noClients, &app, &QCoreApplication::quit);

    auto ret = app.exec();
    SinkLog() << "Exiting: " << instanceIdentifier;
    printStats();
    return ret;
}

#include "main.moc"
