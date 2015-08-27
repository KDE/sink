#pragma once

#include <QObject>
#include <QString>

class Smtp : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString serverUrl READ serverUrl WRITE setServerUrl NOTIFY serverUrlChanged)

public:
    explicit Smtp(QObject *parent = 0);

    QString serverUrl() const;
    void setServerUrl(const QString &url);

signals:
    void serverUrlChanged();

public slots:
    void saveConfig();
    void loadConfig(const QString &id);

private:
    QString m_id;
    QString m_serverUrl;
};
