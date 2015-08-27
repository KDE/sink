#pragma once

#include <QObject>
#include <QString>

class Imap : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString serverUrl READ serverUrl WRITE setServerUrl NOTIFY serverUrlChanged)
    Q_PROPERTY(QString login READ login WRITE setLogin NOTIFY loginChanged)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY passwordChanged)
    Q_PROPERTY(bool disconnectedModeEnabled READ disconnectedModeEnabled WRITE setDisconnectedModeEnabled NOTIFY disconnectedModeEnabledChanged)
    Q_PROPERTY(bool intervalCheckEnabled READ intervalCheckEnabled WRITE setIntervalCheckEnabled NOTIFY intervalCheckEnabledChanged)
    Q_PROPERTY(int checkintervalInMinutes READ checkintervalInMinutes WRITE setCheckintervalInMinutes NOTIFY checkintervalInMinutesChanged)
    Q_PROPERTY(bool serverSieveEnabled READ serverSieveEnabled WRITE setServerSieveEnabled NOTIFY serverSieveEnabledChanged)
    Q_PROPERTY(bool reuseLoginSieve READ reuseLoginSieve WRITE setReuseLoginSieve NOTIFY reuseLoginSieveChanged)
    Q_PROPERTY(int sievePort READ sievePort WRITE setSievePort NOTIFY sievePortChanged)
    Q_PROPERTY(QString sieveUrl READ sieveUrl WRITE setSieveUrl NOTIFY sieveUrlChanged)
    Q_PROPERTY(QString sieveLogin READ sieveLogin WRITE setSieveLogin NOTIFY sieveLoginChanged)
    Q_PROPERTY(QString sievePassword READ sievePassword WRITE setSievePassword NOTIFY sievePasswordChanged)
    //Q_PROPERTY(bool compactFolders READ compactFolders WRITE setCompactFolders NOTIFY compactFoldersChanged)
    Q_PROPERTY(int encryptionType READ encryptionType WRITE setEncryptionType NOTIFY encryptionTypeChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(int authenticationType READ authenticationType WRITE setAuthenticationType NOTIFY authenticationTypeChanged)


public:
    explicit Imap(QObject *parent = 0);

    QString serverUrl() const;
    void setServerUrl(const QString &url);

    QString login() const;
    void setLogin(const QString &login);

    QString password() const;
    void setPassword(const QString &password);

    bool disconnectedModeEnabled() const;
    void setDisconnectedModeEnabled(bool b);

    bool intervalCheckEnabled() const;
    void setIntervalCheckEnabled(bool b);

    int checkintervalInMinutes() const;
    void setCheckintervalInMinutes(int minutes);

    bool serverSieveEnabled() const;
    void setServerSieveEnabled(bool b);

    bool reuseLoginSieve() const;
    void setReuseLoginSieve(bool b);

    int sievePort() const;
    void setSievePort(int port);

    QString sieveUrl() const;
    void setSieveUrl(const QString &url);

    QString sieveLogin() const;
    void setSieveLogin(const QString &login);

    QString sievePassword() const;
    void setSievePassword(const QString &pw);

    //bool compactFolders() const;
    //void setCompactFolders(bool compact);

    int encryptionType() const;
    void setEncryptionType(int encryptionType);

    int port() const;
    void setPort(int port);

    int authenticationType() const;
    void setAuthenticationType(int authType);

signals:
    void serverUrlChanged();
    void loginChanged();
    void passwordChanged();
    void disconnectedModeEnabledChanged();
    void intervalCheckEnabledChanged();
    void checkintervalInMinutesChanged();
    void serverSieveEnabledChanged();
    void reuseLoginSieveChanged();
    void sievePortChanged();
    void sieveUrlChanged();
    void sieveLoginChanged();
    void sievePasswordChanged();
    void authenticationTypeChanged();
    void portChanged();
    void encryptionTypeChanged();
    void compactFoldersChanged();

public slots:
    void saveConfig();
    void loadConfig(const QString &id);

private:
    QString m_id;
    QString m_serverUrl;
    QString m_login;
    QString m_password;
    bool m_disconnectedModeEnabled;
    bool m_intervalCheckEnabled;
    int m_checkintervalInMinutes;
    bool m_serverSieveEnabled;
    bool m_reuseLoginSieve;
    int m_sievePort;
    QString m_sieveUrl;
    QString m_sieveLogin;
    QString m_sievePassword;
    //bool m_serverSideSubscriptionsEnabeld; ////TODO needs akonadi folderpicker
    bool m_compactfolders;
    //QString m_trashFolder; //TODO needs akonadi folderpicker
    int m_encryptionType; //TODO use enum?
    int m_port;
    int m_authenticationType; //TODO use enum?
};