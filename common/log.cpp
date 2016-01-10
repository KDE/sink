#include "log.h"

#include <QString>
#include <QIODevice>
#include <QCoreApplication>
#include <iostream>
#include <unistd.h>

using namespace Akonadi2::Log;

class DebugStream: public QIODevice
{
public:
    QString m_location;
    DebugStream()
        : QIODevice()
    {
        open(WriteOnly);
    }
    virtual ~DebugStream(){};

    bool isSequential() const { return true; }
    qint64 readData(char *, qint64) { return 0; /* eof */ }
    qint64 readLineData(char *, qint64) { return 0; /* eof */ }
    qint64 writeData(const char *data, qint64 len)
    {
        const QByteArray buf = QByteArray::fromRawData(data, len);
        // if (!qgetenv("IMAP_TRACE").isEmpty()) {
            // qt_message_output(QtDebugMsg, buf.trimmed().constData());
            std::cout << buf.trimmed().constData() << std::endl;
        // }
        return len;
    }
private:
    Q_DISABLE_COPY(DebugStream)
};

class NullStream: public QIODevice
{
public:
    NullStream()
        : QIODevice()
    {
        open(WriteOnly);
    }
    virtual ~NullStream(){};

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

QByteArray Akonadi2::Log::debugLevelName(DebugLevel debugLevel)
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

DebugLevel Akonadi2::Log::debugLevelFromName(const QByteArray &name)
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

void Akonadi2::Log::setDebugOutputLevel(DebugLevel debugLevel)
{
    qputenv("AKONADI2DEBUGLEVEL", debugLevelName(debugLevel));
}

Akonadi2::Log::DebugLevel Akonadi2::Log::debugOutputLevel()
{
    return debugLevelFromName(qgetenv("AKONADI2DEBUGLEVEL"));
}

QDebug Akonadi2::Log::debugStream(DebugLevel debugLevel, int line, const char* file, const char* function, const char* debugArea)
{
    DebugLevel debugOutputLevel = debugLevelFromName(qgetenv("AKONADI2DEBUGLEVEL"));
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
        default:
            break;
    };

    bool showLocation = false;
    bool showProgram = true;

    const QString resetColor = colorCommand(ANSI_Colors::Reset);
    QString output;
    output += colorCommand(QList<int>() << ANSI_Colors::Bold << prefixColorCode) + prefix + resetColor;
    if (showProgram) {
        output += QString(" %1(%2)").arg(QString::fromLatin1(programName)).arg(unsigned(getpid()));
    }
    if (debugArea) {
        output += colorCommand(QList<int>() << ANSI_Colors::Bold << prefixColorCode) + QString(" %1 ").arg(QString::fromLatin1(debugArea)) + resetColor;
    }
    if (showLocation) {
        output += QString(" %3").arg(function);
        output += QString("%1:%2").arg(file).arg(line);
    }
    output += ":";

    debug << output;

    return debug;
}
