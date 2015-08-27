#include "imap.h"

#include <QSettings>

Imap::Imap(QObject *parent) : QObject(parent)
{

}

void Imap::saveConfig()
{
    //TODO use the akoandi c++ api

    QSettings settings("AkonadiNext", "imap");
    settings.beginGroup(m_id);

    settings.setValue("serverUrl", m_serverUrl);
    settings.setValue("login", m_login);
    settings.setValue("password", m_password); //TODO store securely

    settings.setValue("disconnectedModeEnabled", m_disconnectedModeEnabled);
    settings.setValue("intervalCheckEnabled", m_intervalCheckEnabled);
    settings.setValue("checkintervalInMinutes", m_checkintervalInMinutes);

    settings.setValue("serverSieveEnabled", m_serverSieveEnabled);
    settings.setValue("reuseLoginSieve", m_reuseLoginSieve);
    settings.setValue("sievePort", m_sievePort);
    settings.setValue("sieveUrl", m_sieveUrl);
    settings.setValue("sieveLogin", m_sieveLogin);
    settings.setValue("sievePassword", m_sievePassword); //TODO store securely

    //settings.setValue("compactFolders", m_compactfolders); FIXME
    /*
    settings.setValue("serverSideSubscirptionsEnabled", m_serverSideSubscriptionsEnabeld);
    settings.setValue("trashFolder", m_trashFolder);
    */

    settings.setValue("encryption", m_encryptionType);
    settings.setValue("port", m_port);
    settings.setValue("authentication", m_authenticationType);

    settings.endGroup();
}

void Imap::loadConfig(const QString &id)
{

    //TODO use the akoandi c++ api

    m_id = id;

    QSettings settings("AkonadiNext", "imap");
    settings.beginGroup(m_id);

    setServerUrl(settings.value("serverUrl").toString());
    setLogin(settings.value("login").toString());
    setPassword(settings.value("password").toString());

    setDisconnectedModeEnabled(settings.value("disconnectedModeEnabled").toBool());
    setIntervalCheckEnabled(settings.value("intervalCheckEnabled").toBool());
    setCheckintervalInMinutes(settings.value("checkintervalInMinutes").toInt());

    setServerSieveEnabled(settings.value("serverSieveEnabled").toBool());
    setReuseLoginSieve(settings.value("reuseLoginSieve").toBool());
    setSievePort(settings.value("sievePort").toInt());
    setSieveUrl(settings.value("sieveUrl").toString());
    setSieveLogin(settings.value("sieveLogin").toString());
    setSievePassword(settings.value("sievePassword").toString());

    //setCompactFolders(settings.value("compactFolders").toBool()); FIXME
    /*
    m_serverSideSubscriptionsEnabeld = settings.value("serverSideSubscirptionsEnabled").toBool();
    m_trashFolder = settings.value("trashFolder").toString();
    */

    setEncryptionType(settings.value("encryption").toInt());
    setPort(settings.value("port").toInt());
    setAuthenticationType(settings.value("authentication").toInt());

    settings.endGroup();
}

QString Imap::serverUrl() const
{
    return m_serverUrl;
}

void Imap::setServerUrl(const QString &url)
{
    if (m_serverUrl != url) {
        m_serverUrl = url;
        emit serverUrlChanged();
    }
}

QString Imap::login() const
{
    return m_login;
}

void Imap::setLogin(const QString &login)
{
    if (login != m_login) {
        m_login = login;
        emit loginChanged();
    }
}

QString Imap::password() const
{
    return m_password;
}

void Imap::setPassword(const QString &password)
{
    if (password != m_password) {
        m_password = password;
        emit passwordChanged();
    }
}

bool Imap::disconnectedModeEnabled() const
{
    return m_disconnectedModeEnabled;
}

void Imap::setDisconnectedModeEnabled(bool b)
{
    if (b != m_disconnectedModeEnabled) {
        m_disconnectedModeEnabled = b;
        emit disconnectedModeEnabledChanged();
    }
}

bool Imap::intervalCheckEnabled() const
{
    return m_intervalCheckEnabled;
}

void Imap::setIntervalCheckEnabled(bool b)
{
    if (m_intervalCheckEnabled = b) {
        m_intervalCheckEnabled = b;
        emit intervalCheckEnabledChanged();
    }
}

int Imap::checkintervalInMinutes() const
{
    return m_checkintervalInMinutes;
}

void Imap::setCheckintervalInMinutes(int minutes)
{
    if (minutes != m_checkintervalInMinutes) {
        m_checkintervalInMinutes = minutes;
        emit checkintervalInMinutesChanged();
    }
}

bool Imap::serverSieveEnabled() const
{
    return m_serverSieveEnabled;
}

void Imap::setServerSieveEnabled(bool b)
{
    if (b != m_serverSieveEnabled) {
        m_serverSieveEnabled = b;
        emit serverSieveEnabledChanged();
    }
}

bool Imap::reuseLoginSieve() const
{
    return m_reuseLoginSieve;
}

void Imap::setReuseLoginSieve(bool b)
{
    if (m_reuseLoginSieve != b) {
        m_reuseLoginSieve = b;
        emit reuseLoginSieveChanged();
    }
}

int Imap::sievePort() const
{
    return m_sievePort;
}

void Imap::setSievePort(int port)
{
    if (m_sievePort != port) {
        m_sievePort = port;
        emit sievePortChanged();
    }
}

QString Imap::sieveUrl() const
{
    return m_sieveUrl;
}

void Imap::setSieveUrl(const QString &url)
{
    if (m_sieveUrl != url) {
        m_sieveUrl = url;
        emit sieveUrlChanged();
    }
}

QString Imap::sieveLogin() const
{
    return m_sieveLogin;
}

void Imap::setSieveLogin(const QString &login)
{
    if (login != m_sieveLogin) {
        m_sieveLogin = login;
        emit sieveLoginChanged();
    }
}

QString Imap::sievePassword() const
{
    return m_sievePassword;
}

void Imap::setSievePassword(const QString &pw)
{
    if (pw != m_sievePassword) {
        m_sievePassword = pw;
        emit sievePasswordChanged();
    }
}

int Imap::encryptionType() const
{
    return m_encryptionType;
}

void Imap::setEncryptionType(int encryptionType)
{
    if (m_encryptionType != encryptionType) {
        m_encryptionType = encryptionType;
        emit encryptionTypeChanged();
    }
}

int Imap::port() const
{
    return m_port;
}

void Imap::setPort(int port)
{
    if (port != m_port) {
        m_port = port;
        emit portChanged();
    }
}

int Imap::authenticationType() const
{
    return m_authenticationType;
}

void Imap::setAuthenticationType(int authType)
{
    if (authType != m_authenticationType) {
        m_authenticationType = authType;
        emit authenticationTypeChanged();
    }
}