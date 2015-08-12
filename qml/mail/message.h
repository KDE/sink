#pragma once

#include "error.h"

#include <QObject>
#include <QString>
#include <QDateTime>

class Message : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString subject READ subject NOTIFY messageChanged)
    Q_PROPERTY(QString from READ from NOTIFY messageChanged)
    Q_PROPERTY(QString to READ to NOTIFY messageChanged)
    Q_PROPERTY(QString cc READ cc NOTIFY messageChanged)
    Q_PROPERTY(QString bcc READ bcc NOTIFY messageChanged)
    Q_PROPERTY(QDateTime date READ date NOTIFY messageChanged)
    Q_PROPERTY(QString textContent READ textContent NOTIFY messageChanged)

public:
    explicit Message(QObject *parent = Q_NULLPTR);
    ~Message();

    QString subject() const;
    QString from() const;
    QString to() const;
    QString cc() const;
    QString bcc() const;
    QString textContent() const;
    QDateTime date() const;

Q_SIGNALS:
    void messageChanged();

public slots:
    void loadMessage();

private:
    QString m_subject;
    QString m_from;
    QString m_to;
    QString m_cc;
    QString m_bcc;
    QDateTime m_date;
    QString m_textContent;
};
