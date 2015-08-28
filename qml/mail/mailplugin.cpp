#include "mailplugin.h"

#include "message.h"
#include "composer.h"

#include <QtQml>

void MailPlugin::registerTypes (const char *uri)
{
    Q_ASSERT(uri == QLatin1String("org.kde.akonadi2.mail"));
    qmlRegisterType<Message>(uri, 1, 0, "Message");
    qmlRegisterType<Composer>(uri, 1, 0, "Composer");
}