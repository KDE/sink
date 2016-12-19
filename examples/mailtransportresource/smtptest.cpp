
#include <QCoreApplication>
#include <KMime/Message>
#include "mailtransport.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(argv[0]);

    auto args = app.arguments();

    auto msg = KMime::Message::Ptr::create();
    msg->to(true)->from7BitString("doe2@example.org");
    msg->from(true)->from7BitString("doe@example.org");
    msg->subject(true)->from7BitString("Subject");
    msg->assemble();

    MailTransport::sendMessage(msg, "smtp://kolab:25", "doe@example.org", "Welcome2KolabSystems", QByteArray{}, MailTransport::Options{});
}
