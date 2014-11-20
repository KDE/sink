#pragma once

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
    static Console *s_output;
};