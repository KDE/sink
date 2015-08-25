#pragma once

#include <QObject>
#include <QString>

class ICalCalendarFile : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString accountId READ accountId WRITE setAccountId NOTIFY accountIdChanged)
    Q_PROPERTY(QString filePath READ filePath WRITE setFilePath NOTIFY filePathChanged)
    Q_PROPERTY(QString displayName READ displayName WRITE setDisplayName NOTIFY displayNameChanged)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY readOnlyChanged)
    Q_PROPERTY(bool monitoringEnabled READ monitoringEnabled WRITE setMonitoringEnabled NOTIFY monitoringEnabledChanged)

public:
    explicit ICalCalendarFile(QObject *parent = 0);

    QString accountId() const;
    void setAccountId(const QString &id);
    QString filePath() const;
    void setFilePath(const QString &filePath);
    QString displayName() const;
    void setDisplayName(const QString &displayName);
    bool readOnly() const;
    void setReadOnly(bool readOnly);
    bool monitoringEnabled() const;
    void setMonitoringEnabled(bool enabled);

signals:
    void filePathChanged();
    void displayNameChanged();
    void readOnlyChanged();
    void monitoringEnabledChanged();
    void accountIdChanged();

public slots:
    void saveConfig();
    void loadConfig(const QString &id);

private:
    QString m_accountId;
    QString m_akonadiId;
    QString m_filePath;
    QString m_displayName;
    bool m_readOnly;
    bool m_monitoringEnabled;
};