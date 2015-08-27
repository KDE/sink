#include "smtp.h"

#include <QSettings>

Smtp::Smtp(QObject *parent) : QObject(parent)
{

}

void Smtp::saveConfig()
{
    QSettings settings("AkonadiNext", "smtp");
    settings.beginGroup(m_id);
    settings.setValue("serverUrl", m_serverUrl);
    settings.endGroup();
}

void Smtp::loadConfig(const QString &id)
{
    m_id = id;

    QSettings settings("AkonadiNext", "smtp");
    settings.beginGroup(m_id);
    setServerUrl(settings.value("serverUrl").toString());
    settings.endGroup();
}

QString Smtp::serverUrl() const
{
    return m_serverUrl;
}

void Smtp::setServerUrl(const QString &url)
{
    if (m_serverUrl != url) {
        m_serverUrl = url;
        emit serverUrlChanged();
    }
}
