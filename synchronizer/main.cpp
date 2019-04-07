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

#include <signal.h>
#ifndef Q_OS_WIN
#include <execinfo.h>
#endif
#include <csignal>
#include <iostream>
#include <cstdlib>
#include <ostream>
#include <sstream>
#include <thread>
#include <chrono>
#ifndef Q_OS_WIN
#include <unistd.h>
#include <cxxabi.h>
#include <dlfcn.h>
#else
#include <io.h>
#include <process.h>
#endif

#ifdef Q_OS_WIN
# if !defined(Q_CC_MINGW) || (defined(Q_CC_MINGW) && defined(__MINGW64_VERSION_MAJOR))
#  include <crtdbg.h>
# endif
#include <windows.h>
#endif

#include "listener.h"
#include "log.h"
#include "test.h"
#include "definitions.h"
#ifdef Q_OS_OSX
#include <CoreFoundation/CoreFoundation.h>
#endif




#if defined(Q_OS_WIN) && !defined(Q_OS_WINRT)

// Helper class for resolving symbol names by dynamically loading "dbghelp.dll".
class DebugSymbolResolver
{
    Q_DISABLE_COPY(DebugSymbolResolver)
public:
    struct Symbol {
        Symbol() : name(Q_NULLPTR), address(0) {}

        const char *name; // Must be freed by caller.
        DWORD64 address;
    };

    explicit DebugSymbolResolver(HANDLE process);
    ~DebugSymbolResolver() { cleanup(); }

    bool isValid() const { return m_symFromAddr; }

    Symbol resolveSymbol(DWORD64 address) const;

private:
    // typedefs from DbgHelp.h/.dll
    struct DBGHELP_SYMBOL_INFO { // SYMBOL_INFO
        ULONG       SizeOfStruct;
        ULONG       TypeIndex;        // Type Index of symbol
        ULONG64     Reserved[2];
        ULONG       Index;
        ULONG       Size;
        ULONG64     ModBase;          // Base Address of module comtaining this symbol
        ULONG       Flags;
        ULONG64     Value;            // Value of symbol, ValuePresent should be 1
        ULONG64     Address;          // Address of symbol including base address of module
        ULONG       Register;         // register holding value or pointer to value
        ULONG       Scope;            // scope of the symbol
        ULONG       Tag;              // pdb classification
        ULONG       NameLen;          // Actual length of name
        ULONG       MaxNameLen;
        CHAR        Name[1];          // Name of symbol
    };

    typedef BOOL (__stdcall *SymInitializeType)(HANDLE, PCSTR, BOOL);
    typedef BOOL (__stdcall *SymFromAddrType)(HANDLE, DWORD64, PDWORD64, DBGHELP_SYMBOL_INFO *);

    void cleanup();

    const HANDLE m_process;
    HMODULE m_dbgHelpLib;
    SymFromAddrType m_symFromAddr;
};

void DebugSymbolResolver::cleanup()
{
    if (m_dbgHelpLib)
        FreeLibrary(m_dbgHelpLib);
    m_dbgHelpLib = 0;
    m_symFromAddr = Q_NULLPTR;
}

DebugSymbolResolver::DebugSymbolResolver(HANDLE process)
    : m_process(process), m_dbgHelpLib(0), m_symFromAddr(Q_NULLPTR)
{
    bool success = false;
    m_dbgHelpLib = LoadLibraryW(L"dbghelp.dll");
    if (m_dbgHelpLib) {
        SymInitializeType symInitialize = (SymInitializeType)(GetProcAddress(m_dbgHelpLib, "SymInitialize"));
        m_symFromAddr = (SymFromAddrType)(GetProcAddress(m_dbgHelpLib, "SymFromAddr"));
        success = symInitialize && m_symFromAddr && symInitialize(process, NULL, TRUE);
    }
    if (!success)
        cleanup();
}

DebugSymbolResolver::Symbol DebugSymbolResolver::resolveSymbol(DWORD64 address) const
{
    // reserve additional buffer where SymFromAddr() will store the name
    struct NamedSymbolInfo : public DBGHELP_SYMBOL_INFO {
        enum { symbolNameLength = 255 };

        char name[symbolNameLength + 1];
    };

    Symbol result;
    if (!isValid())
        return result;
    NamedSymbolInfo symbolBuffer;
    memset(&symbolBuffer, 0, sizeof(NamedSymbolInfo));
    symbolBuffer.MaxNameLen = NamedSymbolInfo::symbolNameLength;
    symbolBuffer.SizeOfStruct = sizeof(DBGHELP_SYMBOL_INFO);
    if (!m_symFromAddr(m_process, address, 0, &symbolBuffer))
        return result;
    result.name = qstrdup(symbolBuffer.Name);
    result.address = symbolBuffer.Address;
    return result;
}

static LONG WINAPI windowsFaultHandler(struct _EXCEPTION_POINTERS *exInfo)
{
    enum { maxStackFrames = 100 };
    char appName[MAX_PATH];
    if (!GetModuleFileNameA(NULL, appName, MAX_PATH))
        appName[0] = 0;
    // const int msecsFunctionTime = qRound(QTestLog::msecsFunctionTime());
    // const int msecsTotalTime = qRound(QTestLog::msecsTotalTime());
    const int msecsFunctionTime = 0;
    const int msecsTotalTime = 0;
    const void *exceptionAddress = exInfo->ExceptionRecord->ExceptionAddress;
    printf("A crash occurred in %s.\n"
           "Function time: %dms Total time: %dms\n\n"
           "Exception address: 0x%p\n"
           "Exception code   : 0x%lx\n",
           appName, msecsFunctionTime, msecsTotalTime,
           exceptionAddress, exInfo->ExceptionRecord->ExceptionCode);

    DebugSymbolResolver resolver(GetCurrentProcess());
    if (resolver.isValid()) {
        DebugSymbolResolver::Symbol exceptionSymbol = resolver.resolveSymbol(DWORD64(exceptionAddress));
        if (exceptionSymbol.name) {
            printf("Nearby symbol    : %s\n", exceptionSymbol.name);
            delete [] exceptionSymbol.name;
        }
        void *stack[maxStackFrames];
        fputs("\nStack:\n", stdout);
        const unsigned frameCount = CaptureStackBackTrace(0, DWORD(maxStackFrames), stack, NULL);
        for (unsigned f = 0; f < frameCount; ++f)     {
            DebugSymbolResolver::Symbol symbol = resolver.resolveSymbol(DWORD64(stack[f]));
            if (symbol.name) {
                printf("#%3u: %s() - 0x%p\n", f + 1, symbol.name, (const void *)symbol.address);
                delete [] symbol.name;
            } else {
                printf("#%3u: Unable to obtain symbol\n", f + 1);
            }
        }
    }

    fputc('\n', stdout);
    fflush(stdout);

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif // Q_OS_WIN) && !Q_OS_WINRT




static Listener *listener = nullptr;

//Print a demangled stacktrace
void printStacktrace()
{
#ifndef Q_OS_WIN
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
#endif
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

QString read(const QString &filename)
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

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsSet("SINK_GDB_DEBUG")) {
#ifndef Q_OS_WIN
        SinkWarning() << "Running resource in debug mode and waiting for gdb to attach: gdb attach " << getpid();
        raise(SIGSTOP);
#endif
    } else {
        // For crashes
        std::signal(SIGSEGV, crashHandler);
        std::signal(SIGABRT, crashHandler);
        std::set_terminate(terminateHandler);
    }

#if defined(Q_OS_WIN)
# ifndef Q_CC_MINGW
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
# endif
# ifndef Q_OS_WINRT
    SetErrorMode(SetErrorMode(0) | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(windowsFaultHandler);
# endif
#endif

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

    QGuiApplication app(argc, argv);
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
    printStats();
    return ret;
}
