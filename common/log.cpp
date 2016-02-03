#include "log.h"

#include <QString>
#include <QIODevice>
#include <QCoreApplication>
#include <iostream>
#include <unistd.h>

using namespace Sink::Log;

class DebugStream: public QIODevice
{
public:
    QString m_location;
    DebugStream()
        : QIODevice()
    {
        open(WriteOnly);
    }
    virtual ~DebugStream();

    bool isSequential() const { return true; }
    qint64 readData(char *, qint64) { return 0; /* eof */ }
    qint64 readLineData(char *, qint64) { return 0; /* eof */ }
    qint64 writeData(const char *data, qint64 len)
    {
        std::cout << data << std::endl;
        return len;
    }
private:
    Q_DISABLE_COPY(DebugStream)
};

//Virtual method anchor
DebugStream::~DebugStream()
{}

class NullStream: public QIODevice
{
public:
    NullStream()
        : QIODevice()
    {
        open(WriteOnly);
    }
    virtual ~NullStream();

    bool isSequential() const { return true; }
    qint64 readData(char *, qint64) { return 0; /* eof */ }
    qint64 readLineData(char *, qint64) { return 0; /* eof */ }
    qint64 writeData(const char *data, qint64 len)
    {
        return len;
    }
private:
    Q_DISABLE_COPY(NullStream)
};

//Virtual method anchor
NullStream::~NullStream()
{}

    /*
     * ANSI color codes:
     * 0: reset colors/style
     * 1: bold
     * 4: underline
     * 30 - 37: black, red, green, yellow, blue, magenta, cyan, and white text
     * 40 - 47: black, red, green, yellow, blue, magenta, cyan, and white background
     */
enum ANSI_Colors {
    DoNothing = -1,
    Reset = 0,
    Bold = 1,
    Underline = 4,
    Black = 30,
    Red = 31,
    Green = 32,
    Yellow = 33,
    Blue = 34
};

static QString colorCommand(int colorCode)
{
    return QString("\x1b[%1m").arg(colorCode);
}

static QString colorCommand(QList<int> colorCodes)
{
    colorCodes.removeAll(ANSI_Colors::DoNothing);
    if (colorCodes.isEmpty()) {
        return QString();
    }
    QString string("\x1b[");
    for (int code : colorCodes) {
        string += QString("%1;").arg(code);
    }
    string.chop(1);
    string += "m";
    return string;
}

QByteArray Sink::Log::debugLevelName(DebugLevel debugLevel)
{
    switch (debugLevel) {
        case DebugLevel::Trace:
            return "Trace";
        case DebugLevel::Log:
            return "Log";
        case DebugLevel::Warning:
            return "Warning";
        case DebugLevel::Error:
            return "Error";
        default:
            break;
    };
    Q_ASSERT(false);
    return QByteArray();
}

DebugLevel Sink::Log::debugLevelFromName(const QByteArray &name)
{
    const QByteArray lowercaseName = name.toLower();
    if (lowercaseName == "trace")
        return DebugLevel::Trace;
    if (lowercaseName == "log")
        return DebugLevel::Log;
    if (lowercaseName == "warning")
        return DebugLevel::Warning;
    if (lowercaseName == "error")
        return DebugLevel::Error;
    return DebugLevel::Log;
}

void Sink::Log::setDebugOutputLevel(DebugLevel debugLevel)
{
    qputenv("SINKDEBUGLEVEL", debugLevelName(debugLevel));
}

Sink::Log::DebugLevel Sink::Log::debugOutputLevel()
{
    return debugLevelFromName(qgetenv("SINKDEBUGLEVEL"));
}

QDebug Sink::Log::debugStream(DebugLevel debugLevel, int line, const char* file, const char* function, const char* debugArea)
{
    DebugLevel debugOutputLevel = debugLevelFromName(qgetenv("SINKDEBUGLEVEL"));
    if (debugLevel < debugOutputLevel) {
        static NullStream stream;
        return QDebug(&stream);
    }

    static DebugStream stream;
    QDebug debug(&stream);

    static QByteArray programName;
    if (programName.isEmpty()) {
        if (QCoreApplication::instance())
            programName = QCoreApplication::instance()->applicationName().toLocal8Bit();
        else
            programName = "<unknown program name>";
    }

    QString prefix;
    int prefixColorCode = ANSI_Colors::DoNothing;
    switch (debugLevel) {
        case DebugLevel::Trace:
            prefix = "Trace:  ";
            break;
        case DebugLevel::Log:
            prefix = "Log:    ";
            prefixColorCode = ANSI_Colors::Green;
            break;
        case DebugLevel::Warning:
            prefix = "Warning:";
            prefixColorCode = ANSI_Colors::Red;
            break;
        case DebugLevel::Error:
            prefix = "Error:  ";
            prefixColorCode = ANSI_Colors::Red;
            break;
    };

    bool showLocation = false;
    bool showFunction = false;
    bool showProgram = false;
    bool useColor = true;
    bool multiline = false;

    const QString resetColor = colorCommand(ANSI_Colors::Reset);
    QString output;
    if (useColor) {
        output += colorCommand(QList<int>() << ANSI_Colors::Bold << prefixColorCode);
    }
    output += prefix;
    if (useColor) {
        output += resetColor;
    }
    if (showProgram) {
        int width = 10;
        output += QString(" %1(%2)").arg(QString::fromLatin1(programName).leftJustified(width, ' ', true)).arg(unsigned(getpid())).rightJustified(width + 8, ' ');
    }
    if (debugArea) {
        if (useColor) {
            output += colorCommand(QList<int>() << ANSI_Colors::Bold << prefixColorCode);
        }
        output += QString(" %1 ").arg(QString::fromLatin1(debugArea).leftJustified(25, ' ', true));
        if (useColor) {
            output += resetColor;
        }
    }
    if (showFunction) {
        output += QString(" %3").arg(QString::fromLatin1(function).leftJustified(25, ' ', true));
    }
    if (showLocation) {
        const auto filename = QString::fromLatin1(file).split('/').last();
        output += QString(" %1:%2").arg(filename.right(25)).arg(QString::number(line).leftJustified(4, ' ')).leftJustified(30, ' ', true);
    }
    if (multiline) {
        output += "\n  ";
    }
    output += ": ";

    debug.noquote().nospace() << output;

    return debug;
}
