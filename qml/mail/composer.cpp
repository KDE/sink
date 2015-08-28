#include "composer.h"

Composer::Composer( QObject* parent ) : QObject( parent )
{

}

QString Composer::to() const
{
    return m_to;
}

void Composer::setTo(const QString& to)
{
    if ( to != m_to ) {
        m_to = to;
        emit toChanged();
    }
}

QString Composer::subject() const
{
    return m_subject;
}

void Composer::setSubject(const QString& subject)
{
    if ( subject != m_subject ) {
        m_subject = subject;
        emit subjectChanged();
    }
}

QString Composer::body() const
{
    return m_body;
}

void Composer::setBody(const QString& body)
{
    if ( body != m_body ) {
        m_body = body;
        emit bodyChanged();
    }
}


