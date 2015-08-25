#include "icalcalendarfile.h"

#include <QSettings>
#include <QDebug>

ICalCalendarFile::ICalCalendarFile(QObject *parent) : QObject(parent), m_readOnly(false), m_monitoringEnabled(false)
{

}

void ICalCalendarFile::loadConfig(const QString &id)
{
    m_accountId = id;

    QSettings accountSettings("AkonadiNext", "accounts");
    accountSettings.beginGroup(m_accountId);
    m_akonadiId = accountSettings.value("akonadi_id").toString();
    accountSettings.endGroup();

    QSettings settings("AkonadiNext", "icalfiles");
    settings.beginGroup(m_akonadiId);
    setDisplayName(settings.value("displayName").toString());
    setFilePath(settings.value("filePath").toString());
    setReadOnly(settings.value("readOnly").toBool());
    setMonitoringEnabled(settings.value("monitoringEnabled").toBool());
    settings.endGroup();
}

void ICalCalendarFile::saveConfig()
{
    QSettings settings("AkonadiNext", "icalfiles");
    settings.beginGroup(m_akonadiId);
    settings.setValue("displayName", m_displayName);
    settings.setValue("filePath", m_filePath);
    settings.setValue("readOnly", m_readOnly);
    settings.setValue("monitoringEnabled", m_monitoringEnabled);
    settings.endGroup();
}

QString ICalCalendarFile::accountId() const
{
    return m_accountId;
}

void ICalCalendarFile::setAccountId(const QString &id)
{
    if (m_accountId != id) {
        loadConfig(id);
        emit accountIdChanged();
    }
}

QString ICalCalendarFile::displayName() const
{
    return m_displayName;
}

void ICalCalendarFile::setDisplayName(const QString &displayName)
{
    if (m_displayName != displayName) {
        m_displayName = displayName;
        emit displayNameChanged();
    }
}

QString ICalCalendarFile::filePath() const
{
    return m_filePath;
}

void ICalCalendarFile::setFilePath(const QString &filePath)
{
    if (m_filePath != filePath) {
        m_filePath = filePath;
        emit filePathChanged();
    }
}

bool ICalCalendarFile::readOnly() const
{
    return m_readOnly;
}

void ICalCalendarFile::setReadOnly(bool readOnly)
{
    if (m_readOnly != readOnly) {
        m_readOnly = readOnly;
        emit readOnlyChanged();
    }
}

bool ICalCalendarFile::monitoringEnabled() const
{
    return m_monitoringEnabled;
}

void ICalCalendarFile::setMonitoringEnabled(bool enabled)
{
    if (m_monitoringEnabled != enabled) {
        m_monitoringEnabled = enabled;
        emit monitoringEnabledChanged();
    }
}
