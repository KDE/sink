#include "log.h"

#include <QString>
#include <QDir>
#include <QIODevice>
#include <QCoreApplication>
#include <QSettings>
#include <QSharedPointer>
#include <QMutex>
#include <QMutexLocker>
#include <iostream>
#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif
#include <atomic>
#include <definitions.h>
#include <QThreadStorage>
#include <QStringBuilder>

using namespace Sink::Log;

static QThreadStorage<QSharedPointer<QSettings>> sSettings;
static QSettings &config()
{
    if (!sSettings.hasLocalData()) {
        sSettings.setLocalData(QSharedPointer<QSettings>::create(Sink::configLocation() + "/log.ini", QSettings::IniFormat));
    }
    return *sSettings.localData();
}

Q_GLOBAL_STATIC(QByteArray, sPrimaryComponent);

void Sink::Log::setPrimaryComponent(const QString &component)
{
    if (!sPrimaryComponent.isDestroyed()) {
        *sPrimaryComponent = component.toUtf8();
    }
}

class DebugStream : public QIODevice
{
public:
    QString m_location;
    DebugStream() : QIODevice()
    {
        open(WriteOnly);
    }
    virtual ~DebugStream();

    bool isSequential() const
    {
        return true;
    }
    qint64 readData(char *, qint64)
    {
        return 0; /* eof */
    }
    qint64 readLineData(char *, qint64)
    {
        return 0; /* eof */
    }
    qint64 writeData(const char *data, qint64 len)
    {
#ifdef Q_OS_WIN
        const auto string = QString::fromUtf8(data, leng)
        OutputDebugString(reinterpret_cast<const wchar_t*>(string.utf16()));
#else
        std::cout << data << std::endl;
#endif
        return len;
    }

private:
    Q_DISABLE_COPY(DebugStream)
};

// Virtual method anchor
DebugStream::~DebugStream()
{
}

class NullStream : public QIODevice
{
public:
    NullStream() : QIODevice()
    {
        open(WriteOnly);
    }
    virtual ~NullStream();

    bool isSequential() const
    {
        return true;
    }
    qint64 readData(char *, qint64)
    {
        return 0; /* eof */
    }
    qint64 readLineData(char *, qint64)
    {
        return 0; /* eof */
    }
    qint64 writeData(const char *data, qint64 len)
    {
        return len;
    }

private:
    Q_DISABLE_COPY(NullStream)
};

// Virtual method anchor
NullStream::~NullStream()
{
}

/*
 * ANSI color codes:
 * 0: reset colors/style
 * 1: bold
 * 4: underline
 * 30 - 37: black, red, green, yellow, blue, magenta, cyan, and white text
 * 40 - 47: black, red, green, yellow, blue, magenta, cyan, and white background
 */
enum ANSI_Colors
{
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
    config().setValue("level", debugLevel);
}

Sink::Log::DebugLevel Sink::Log::debugOutputLevel()
{
    return static_cast<Sink::Log::DebugLevel>(config().value("level", Sink::Log::Log).toInt());
}

void Sink::Log::setDebugOutputFilter(FilterType type, const QByteArrayList &filter)
{
    switch (type) {
        case ApplicationName:
            config().setValue("applicationfilter", QVariant::fromValue(filter));
            break;
        case Area:
            config().setValue("areafilter", QVariant::fromValue(filter));
            break;
    }
}

QByteArrayList Sink::Log::debugOutputFilter(FilterType type)
{
    switch (type) {
        case ApplicationName:
            return config().value("applicationfilter").value<QByteArrayList>();
        case Area:
            return config().value("areafilter").value<QByteArrayList>();
        default:
            return QByteArrayList();
    }
}

void Sink::Log::setDebugOutputFields(const QByteArrayList &output)
{
    config().setValue("outputfields", QVariant::fromValue(output));
}

QByteArrayList Sink::Log::debugOutputFields()
{
    return config().value("outputfields").value<QByteArrayList>();
}

static QByteArray getProgramName()
{
    if (QCoreApplication::instance()) {
        return QCoreApplication::instance()->applicationName().toLocal8Bit();
    } else {
        return "<unknown program name>";
    }
}

static QSharedPointer<QSettings> debugAreasConfig()
{
    return QSharedPointer<QSettings>::create(Sink::dataLocation() + "/debugAreas.ini", QSettings::IniFormat);
}

class DebugAreaCollector {
public:
    DebugAreaCollector()
    {
        QMutexLocker locker(&mutex);
        mDebugAreas = debugAreasConfig()->value("areas").value<QString>().split(';').toSet();
    }

    ~DebugAreaCollector()
    {
        QMutexLocker locker(&mutex);
        mDebugAreas += debugAreasConfig()->value("areas").value<QString>().split(';').toSet();
        debugAreasConfig()->setValue("areas", QVariant::fromValue(mDebugAreas.toList().join(';')));
    }

    void add(const QString &area)
    {
        QMutexLocker locker(&mutex);
        mDebugAreas << area;
    }

    QSet<QString> debugAreas()
    {
        QMutexLocker locker(&mutex);
        return mDebugAreas;
    }

    QMutex mutex;
    QSet<QString> mDebugAreas;
};

Q_GLOBAL_STATIC(DebugAreaCollector, sDebugAreaCollector);

QSet<QString> Sink::Log::debugAreas()
{
    if (!sDebugAreaCollector.isDestroyed()) {
        return sDebugAreaCollector->debugAreas();
    }
    return {};
}

static void collectDebugArea(const QString &debugArea)
{
    if (!sDebugAreaCollector.isDestroyed()) {
        sDebugAreaCollector->add(debugArea);
    }
}

static bool containsItemStartingWith(const QByteArray &pattern, const QByteArrayList &list)
{
    for (const auto &item : list) {
        int start = 0;
        int end = item.size();
        if (item.startsWith('*')) {
            start++;
        }
        if (item.endsWith('*')) {
            end--;
        }
        if (pattern.contains(item.mid(start, end - start))) {
            return true;
        }
    }
    return false;
}

static bool caseInsensitiveContains(const QByteArray &pattern, const QByteArrayList &list)
{
    for (const auto &item : list) {
        if (item.toLower() == pattern) {
            return true;
        }
    }
    return false;
}

static QByteArray getFileName(const char *file)
{
    static char sep = QDir::separator().toLatin1();
    auto filename = QByteArray(file).split(sep).last();
    return filename.split('.').first();
}

static QString assembleDebugArea(const char *debugArea, const char *debugComponent, const char *file)
{
    if (!sPrimaryComponent.isDestroyed() && sPrimaryComponent->isEmpty()) {
        *sPrimaryComponent = getProgramName();
    }
    if (!sPrimaryComponent.isDestroyed()) {
        //Using stringbuilder for fewer allocations
        return QLatin1String{*sPrimaryComponent} % QLatin1String{"."} %
            (debugComponent ? (QLatin1String{debugComponent} + QLatin1String{"."}) : QLatin1String{""}) %
            (debugArea ? QLatin1String{debugArea} : QLatin1String{getFileName(file)});
    } else {
        return {};
    }
}

static bool isFiltered(DebugLevel debugLevel, const QByteArray &fullDebugArea)
{
    if (debugLevel < debugOutputLevel()) {
        return true;
    }
    const auto areas = debugOutputFilter(Sink::Log::Area);
    if ((debugLevel <= Sink::Log::Trace) && !areas.isEmpty()) {
        if (!containsItemStartingWith(fullDebugArea, areas)) {
            return true;
        }
    }
    return false;
}

bool Sink::Log::isFiltered(DebugLevel debugLevel, const char *debugArea, const char *debugComponent, const char *file)
{
    return isFiltered(debugLevel, assembleDebugArea(debugArea, debugComponent, file).toLatin1());
}

Q_GLOBAL_STATIC(NullStream, sNullStream);
Q_GLOBAL_STATIC(DebugStream, sDebugStream);

QDebug Sink::Log::debugStream(DebugLevel debugLevel, int line, const char *file, const char *function, const char *debugArea, const char *debugComponent)
{
    const auto fullDebugArea = assembleDebugArea(debugArea, debugComponent, file);
    collectDebugArea(fullDebugArea);

    if (isFiltered(debugLevel, fullDebugArea.toLatin1())) {
        if (!sNullStream.isDestroyed()) {
            return QDebug(sNullStream);
        }
        return QDebug{QtDebugMsg};
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

    auto debugOutput = debugOutputFields();

    bool showLocation = debugOutput.isEmpty() ? false : caseInsensitiveContains("location", debugOutput);
    bool showFunction = debugOutput.isEmpty() ? false : caseInsensitiveContains("function", debugOutput);
    bool showProgram = debugOutput.isEmpty() ? false : caseInsensitiveContains("application", debugOutput);
#ifdef Q_OS_WIN
    bool useColor = false;
#else
    bool useColor = true;
#endif
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
        output += QString(" %1(%2)").arg(QString::fromLatin1(getProgramName()).leftJustified(width, ' ', true)).arg(unsigned(getpid())).rightJustified(width + 8, ' ');
    }
    if (useColor) {
        output += colorCommand(QList<int>() << ANSI_Colors::Bold << prefixColorCode);
    }
    static std::atomic<int> maxDebugAreaSize{25};
    maxDebugAreaSize = qMax(fullDebugArea.size(), maxDebugAreaSize.load());
    output += QString(" %1 ").arg(fullDebugArea.leftJustified(maxDebugAreaSize, ' ', false));
    if (useColor) {
        output += resetColor;
    }
    if (showFunction) {
        output += QString(" %3").arg(fullDebugArea.leftJustified(25, ' ', true));
    }
    if (showLocation) {
        const auto filename = QString::fromLatin1(file).split('/').last();
        output += QString(" %1:%2").arg(filename.right(25)).arg(QString::number(line).leftJustified(4, ' ')).leftJustified(30, ' ', true);
    }
    if (multiline) {
        output += "\n  ";
    }
    output += ":";

    if (sDebugStream.isDestroyed()) {
        return QDebug{QtDebugMsg};
    }
    QDebug debug(sDebugStream);
    debug.noquote().nospace() << output;
    return debug.space().quote();
}
