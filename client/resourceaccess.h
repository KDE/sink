#pragma once

#include <QLocalSocket>
#include <QObject>
#include <QTimer>

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
    void revisionChanged(unsigned long long revision);

private Q_SLOTS:
    void connected();
    void disconnected();
    void connectionError(QLocalSocket::LocalSocketError error);
    void readResourceMessage();
    bool processMessageBuffer();

private:
    QString m_resourceName;
    QLocalSocket *m_socket;
    QTimer *m_tryOpenTimer;
    bool m_startingProcess;
    QByteArray m_partialMessageBuffer;
};
