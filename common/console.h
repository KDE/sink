#pragma once

#include <akonadi2common_export.h>

#include <QTime>
#include <QWidget>

class QTextBrowser;

namespace Akonadi2
{

class AKONADI2COMMON_EXPORT Console : public QWidget
{
    Q_OBJECT
public:
    static Console *main();
    Console(const QString &title);
    ~Console();

    void log(const QString &message);

private:
    QTextBrowser *m_textDisplay;
    QTime m_timestamper;
    static Console *s_output;
};

} // namespace Akonadi2
