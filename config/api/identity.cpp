#include "identity.h"

#include <QSettings>

Identity::Identity(QObject *parent) : QObject(parent)
{

}

void Identity::saveConfig()
{
    QSettings settings("AkonadiNext", "identities");
    settings.beginGroup(m_id);
    settings.setValue("signature", m_signature);
    settings.endGroup();
}

void Identity::loadConfig(const QString &id)
{
    m_id = id;

    QSettings settings("AkonadiNext", "identities");
    settings.beginGroup(m_id);
    setSignature(settings.value("signature").toString());
    settings.endGroup();
}


QString Identity::signature() const
{
    return m_signature;
}

void Identity::setSignature(const QString &signature)
{
    if (m_signature != signature) {
        m_signature = signature;
        emit signatureChanged();
    }
}
