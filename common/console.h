#pragma once

#include <QTime>
#include <QWidget>

class QTextBrowser;

class Console : public QWidget
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
