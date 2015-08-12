#include "message.h"

Message::Message(QObject *parent) : QObject(parent)
{

}

Message::~Message()
{

}

QString Message::subject() const
{
    return m_subject;
}

QString Message::from() const
{
    return m_from;
}

QString Message::to() const
{
    return m_to;
}

QString Message::cc() const
{
    return m_cc;
}

QString Message::bcc() const
{
    return m_bcc;
}

QString Message::textContent() const
{
    return m_textContent;
}

QDateTime Message::date() const
{
    return m_date;
}

void Message::loadMessage()
{
    //TODO connect to Akonadi2 C++ API / loadMessage(akonadiID)

     m_subject = QString("test Subject");
     m_from = QString("testSender@mail.test");
     m_to = QString("Me");
     m_cc = QString("testFriend@ccmail.test");
     m_bcc = QString("testFriend2@bccmail.test");
     m_textContent = QString("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.");
     m_date = QDateTime(QDate(2015, 5, 8), QTime(20, 30, 0));

     emit messageChanged();
}