#pragma once

#include <QLocalSocket>
#include <QObject>

class ResourceAccess : public QObject
{
    Q_OBJECT

public:
    ResourceAccess(const QString &resourceName, QObject *parent = 0);
    ~ResourceAccess();

    QString resourceName() const;
    bool isReady() const;

public Q_SLOTS:
    void open();
    void close();

Q_SIGNALS:
    void ready(bool isReady);

private Q_SLOTS:
    void connected();
    void disconnected();
    void connectionError(QLocalSocket::LocalSocketError error);

private:
    QString m_resourceName;
    QLocalSocket *m_socket;
    bool m_startingProcess;
};
