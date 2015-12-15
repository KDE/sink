#include <QtTest>

#include <QString>

#include "maildirresource/maildirresource.h"
#include "clientapi.h"
#include "commands.h"
#include "entitybuffer.h"
#include "resourceconfig.h"
#include "modelresult.h"
#include "pipeline.h"
#include "log.h"

/**
 * Test of complete system using the maildir resource.
 * 
 * This test requires the maildir resource installed.
 */
class MaildirResourceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        Akonadi2::Log::setDebugOutputLevel(Akonadi2::Log::Trace);
        auto factory = Akonadi2::ResourceFactory::load("org.kde.maildir");
        QVERIFY(factory);
        MaildirResource::removeFromDisk("org.kde.maildir.instance1");
        Akonadi2::ApplicationDomain::AkonadiResource resource;
        resource.setProperty("identifier", "org.kde.maildir.instance1");
        resource.setProperty("type", "org.kde.maildir");
        resource.setProperty("path", "/work/build/local-mail");
        Akonadi2::Store::create(resource).exec().waitForFinished();
    }

    void cleanup()
    {
        Akonadi2::Store::shutdown(QByteArray("org.kde.maildir.instance1")).exec().waitForFinished();
        MaildirResource::removeFromDisk("org.kde.maildir.instance1");
    }

    void init()
    {
        qDebug();
        qDebug() << "-----------------------------------------";
        qDebug();
    }

    void testListFolders()
    {
        Akonadi2::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.syncOnDemand = true;
        query.processAll = true;

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
        QTRY_VERIFY(model->rowCount(QModelIndex()) > 1);
    }

};

QTEST_MAIN(MaildirResourceTest)
#include "maildirresourcetest.moc"
