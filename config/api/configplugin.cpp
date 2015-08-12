#include "configplugin.h"

#include "identity.h"
#include "imap.h"
#include "smtp.h"
#include "imapaccount.h"
#include "icalcalendarfile.h"

#include <QtQml>

void ConfigPlugin::registerTypes (const char *uri)
{
    Q_ASSERT(uri == QLatin1String("org.kde.akonadi2.config"));
}