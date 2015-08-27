#pragma once

#include "imap.h"
#include "smtp.h"
#include "identity.h"

#include <QObject>
#include <QString>
#include <QScopedPointer>

class ImapAccount : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString accountId READ accountId WRITE setAccountId NOTIFY accountIdChanged)
    Q_PROPERTY(Identity* identity READ identity CONSTANT)
    Q_PROPERTY(Smtp* smtp READ smtp CONSTANT)
    Q_PROPERTY(Imap* imap READ imap CONSTANT)

public:
    explicit ImapAccount(QObject *parent = 0);

    QString accountId() const;
    void setAccountId(const QString &accountId);

    Identity *identity() const;
    Smtp *smtp() const;
    Imap *imap() const;

signals:
    void accountIdChanged();

public slots:
    void loadAccount(const QString &accountId);
    void saveConfig();
    void createAccount(const QString &account_id, const QString &smtp_id, const QString &imap_id, const QString &identity_id);

private:
    QString m_accountId;

    QScopedPointer<Identity> m_identity;
    QScopedPointer<Imap> m_imap;
    QScopedPointer<Smtp> m_smtp;
};
