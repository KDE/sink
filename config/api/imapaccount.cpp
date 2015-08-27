#include "imapaccount.h"

#include <QSettings>

ImapAccount::ImapAccount(QObject *parent) : QObject(parent), m_smtp(new Smtp()), m_imap(new Imap()), m_identity(new Identity())
{

}

QString ImapAccount::accountId() const
{
    return m_accountId;
}

void ImapAccount::setAccountId(const QString &accountId)
{
    if (accountId != m_accountId) {
        loadAccount(accountId);
        emit accountIdChanged();
    }
}


void ImapAccount::loadAccount(const QString &accountId)
{
    m_accountId = accountId;

    QSettings settings("AkonadiNext", "accounts");
    settings.beginGroup(accountId);
    m_imap->loadConfig(settings.value("imap_id").toString());
    m_identity->loadConfig(settings.value("identity_id").toString());
    m_smtp->loadConfig(settings.value("smtp_id").toString());
    settings.endGroup();
}

void ImapAccount::saveConfig()
{
    m_identity->saveConfig();
    m_imap->saveConfig();
    m_smtp->saveConfig();
}


void ImapAccount::createAccount(const QString &account_id, const QString &smtp_id, const QString &imap_id, const QString &identity_id)
{
    QSettings settings("AkonadiNext", "accounts");
    settings.beginGroup(account_id);
    settings.setValue("imap_id", imap_id);
    settings.setValue("smtp_id", smtp_id);
    settings.setValue("identity_id", identity_id);
    settings.endGroup();
}

Identity* ImapAccount::identity() const
{
    return m_identity.data();
}

Smtp* ImapAccount::smtp() const
{
    return m_smtp.data();
}

Imap* ImapAccount::imap() const
{
    return m_imap.data();
}
