#include <QtTest>

#include <QString>
#include <KMime/Message>

#include "maildirresource/maildirresource.h"
#include "store.h"
#include "resourcecontrol.h"
#include "commands.h"
#include "entitybuffer.h"
#include "resourceconfig.h"
#include "modelresult.h"
#include "pipeline.h"
#include "log.h"
#include "test.h"

#define ASYNCCOMPARE(actual, expected) \
do {\
    if (!QTest::qCompare(actual, expected, #actual, #expected, __FILE__, __LINE__))\
        return KAsync::error<void>(1, "Comparison failed.");\
} while (0)

#define ASYNCVERIFY(statement) \
do {\
    if (!QTest::qVerify((statement), #statement, "", __FILE__, __LINE__))\
        return KAsync::error<void>(1, "Verify failed.");\
} while (0)

using namespace Sink;

static bool copyRecursively(const QString &srcFilePath, const QString &tgtFilePath)
{
    QFileInfo srcFileInfo(srcFilePath);
    if (srcFileInfo.isDir()) {
        QDir targetDir(tgtFilePath);
        targetDir.cdUp();
        if (!targetDir.mkdir(QFileInfo(srcFilePath).fileName())) {
            qWarning() << "Failed to create directory " << tgtFilePath;
            return false;
        }
        QDir sourceDir(srcFilePath);
        QStringList fileNames = sourceDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        foreach (const QString &fileName, fileNames) {
            const QString newSrcFilePath = srcFilePath + QLatin1Char('/') + fileName;
            const QString newTgtFilePath = tgtFilePath + QLatin1Char('/') + fileName;
            if (!copyRecursively(newSrcFilePath, newTgtFilePath))
                return false;
        }
    } else {
        if (!QFile::copy(srcFilePath, tgtFilePath)) {
            qWarning() << "Failed to copy file " << tgtFilePath;
            return false;
        }
    }
    return true;
}

/**
 * Test of complete system using the maildir resource.
 *
 * This test requires the maildir resource installed.
 */
class MaildirResourceTest : public QObject
{
    Q_OBJECT

    QTemporaryDir tempDir;
    QString targetPath;
private slots:
    void initTestCase()
    {
        targetPath = tempDir.path() + "/maildir1/";

        Sink::Log::setDebugOutputLevel(Sink::Log::Trace);
        MaildirResource::removeFromDisk("org.kde.maildir.instance1");
        Sink::ApplicationDomain::SinkResource resource;
        resource.setProperty("identifier", "org.kde.maildir.instance1");
        resource.setProperty("type", "org.kde.maildir");
        resource.setProperty("path", targetPath);
        Sink::Store::create(resource).exec().waitForFinished();
    }

    void cleanup()
    {
        Sink::ResourceControl::shutdown(QByteArray("org.kde.maildir.instance1")).exec().waitForFinished();
        MaildirResource::removeFromDisk("org.kde.maildir.instance1");
        QDir dir(targetPath);
        dir.removeRecursively();
    }

    void init()
    {
        qDebug();
        qDebug() << "-----------------------------------------";
        qDebug();
        copyRecursively(TESTDATAPATH "/maildir1", targetPath);
        Sink::ResourceControl::start(QByteArray("org.kde.maildir.instance1")).exec().waitForFinished();
    }

    void testListFolders()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";

        // Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 3);
    }

    void testListFolderTree()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.parentProperty = "parent";

        // Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 1);
        auto parentIndex = model->index(0, 0, QModelIndex());
        model->fetchMore(parentIndex);
        QTRY_VERIFY(model->data(parentIndex, Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(parentIndex), 2);
    }

    void testListMailsOfFolder()
    {
        using namespace Sink;
        using namespace Sink::ApplicationDomain;
        // Ensure all local data is processed
        auto query = Query::ResourceFilter("org.kde.maildir.instance1");
        Store::synchronize(query).exec().waitForFinished();
        ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();
        auto result = Store::fetchOne<Folder>(Query::ResourceFilter("org.kde.maildir.instance1") + Query::RequestedProperties(QByteArrayList() << "name"))
                          .then<QList<Mail::Ptr>, Folder>([](const Folder &folder) {
                              Trace() << "Found a folder" << folder.identifier();
                              return Store::fetchAll<Mail>(Query::PropertyFilter("folder", folder) + Query::RequestedProperties(QByteArrayList() << "folder"
                                                                                                                                                 << "subject"));
                          })
                          .then<void, QList<Mail::Ptr>>([](const QList<Mail::Ptr> &mails) { QVERIFY(mails.size() >= 1); })
                          .exec();
        result.waitForFinished();
        QVERIFY(!result.errorCode());
    }

    void testMailContent()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.requestedProperties << "folder"
                                  << "subject"
                                  << "mimeMessage"
                                  << "date";

        // Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto mailModel = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(mailModel->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QVERIFY(mailModel->rowCount(QModelIndex()) >= 1);
        auto mail = mailModel->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Mail::Ptr>();
        QVERIFY(!mail->getProperty("subject").toString().isEmpty());
        QVERIFY(!mail->getProperty("mimeMessage").toString().isEmpty());
        QVERIFY(mail->getProperty("date").toDateTime().isValid());

        QFileInfo info(mail->getProperty("mimeMessage").toString());
        QVERIFY(info.exists());
    }


    void testSyncFolderMove()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.requestedProperties << "name";

        // Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto targetPath = tempDir.path() + "/maildir1/";
        QDir dir(targetPath);
        QVERIFY(dir.rename("inbox", "newbox"));

        // Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 3);
        QCOMPARE(model->match(model->index(0, 0, QModelIndex()), Qt::DisplayRole, QStringLiteral("newbox"), 1).size(), 1);
    }

    void testReSyncMail()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.requestedProperties << "folder"
                                  << "subject";

        // Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        // Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto mailModel = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(mailModel->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(mailModel->rowCount(QModelIndex()), 3);
    }

    void testSyncMailRemoval()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.requestedProperties << "folder"
                                  << "subject";

        // Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto targetPath = tempDir.path() + "/maildir1/cur/1365777830.R28.localhost.localdomain:2,S";
        QFile file(targetPath);
        QVERIFY(file.remove());

        // Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto mailModel = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(mailModel->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(mailModel->rowCount(QModelIndex()), 2);
    }

    void testCreateFolder()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        Sink::ApplicationDomain::Folder folder("org.kde.maildir.instance1");
        folder.setProperty("name", "testCreateFolder");

        Sink::Store::create(folder).exec().waitForFinished();

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto targetPath = tempDir.path() + "/maildir1/testCreateFolder";
        QFileInfo file(targetPath);
        QTRY_VERIFY(file.exists());
        QVERIFY(file.isDir());
    }

    void testRemoveFolder()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";

        auto targetPath = tempDir.path() + "/maildir1/testCreateFolder";

        Sink::ApplicationDomain::Folder folder("org.kde.maildir.instance1");
        folder.setProperty("name", "testCreateFolder");
        Sink::Store::create(folder).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();
        QTRY_VERIFY(QFileInfo(targetPath).exists());

        Sink::Query folderQuery;
        folderQuery.resources << "org.kde.maildir.instance1";
        folderQuery += Sink::Query::PropertyFilter("name", "testCreateFolder");
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(folderQuery);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 1);
        auto createdFolder = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Folder::Ptr>();

        Sink::Store::remove(*createdFolder).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();
        QTRY_VERIFY(!QFileInfo(targetPath).exists());
    }

    void testCreateMail()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto message = KMime::Message::Ptr::create();
        message->subject(true)->fromUnicodeString(QString::fromLatin1("Foobar"), "utf8");
        message->assemble();

        Sink::ApplicationDomain::Mail mail("org.kde.maildir.instance1");
        mail.setBlobProperty("mimeMessage", message->encodedContent());

        Sink::Store::create(mail).exec().waitForFinished();

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto targetPath = tempDir.path() + "/maildir1/new";
        QDir dir(targetPath);
        dir.setFilter(QDir::Files);
        QTRY_COMPARE(dir.count(), static_cast<unsigned int>(1));
        QFile file(targetPath + "/" + dir.entryList().first());
        QVERIFY(file.open(QIODevice::ReadOnly));
        KMime::Message m;
        m.setContent(file.readAll());
        m.parse();
        QCOMPARE(m.subject(true)->asUnicodeString(), QString::fromLatin1("Foobar"));
    }

    void testCreateMailInFolder()
    {
        auto query = Sink::Query::ResourceFilter("org.kde.maildir.instance1");

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto folder = Sink::ApplicationDomain::ApplicationDomainType::createEntity<Sink::ApplicationDomain::Folder>("org.kde.maildir.instance1");
        folder.setProperty("name", "newfolder");

        Sink::Store::create(folder).exec().waitForFinished();

        auto message = KMime::Message::Ptr::create();
        message->subject(true)->fromUnicodeString(QString::fromLatin1("Foobar"), "utf8");
        message->assemble();

        auto mail = Sink::ApplicationDomain::ApplicationDomainType::createEntity<Sink::ApplicationDomain::Mail>("org.kde.maildir.instance1");
        mail.setBlobProperty("mimeMessage", message->encodedContent());
        mail.setProperty("folder", folder);

        Sink::Store::create(mail).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto future = Sink::Store::fetchOne<ApplicationDomain::Mail>(Query::IdentityFilter(mail.identifier()) + Query::RequestedProperties(QByteArrayList() << "mimeMessage" << "folder"))
            .then<void, ApplicationDomain::Mail>([folder](const ApplicationDomain::Mail &mail) {
                QCOMPARE(mail.getProperty("folder").toByteArray(), folder.identifier());
            }).exec();
        future.waitForFinished();
        if (future.errorCode()) {
            qWarning() << future.errorCode() << future.errorMessage();
        }
        QVERIFY(!future.errorCode());
        auto future2 = ResourceControl::inspect<ApplicationDomain::Mail>(ResourceControl::Inspection::ExistenceInspection(mail, false)).exec();
        future2.waitForFinished();
        QVERIFY(!future2.errorCode());

        auto targetPath = tempDir.path() + "/maildir1/newfolder/new";
        QDir dir(targetPath);
        dir.setFilter(QDir::Files);
        QTRY_COMPARE(dir.count(), static_cast<unsigned int>(1));
    }

    void testRemoveMail()
    {
        using namespace Sink;
        using namespace Sink::ApplicationDomain;

        auto query = Query::ResourceFilter("org.kde.maildir.instance1");
        Store::synchronize(query).exec().waitForFinished();
        ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto result = Store::fetchOne<Folder>(
                          Query::ResourceFilter("org.kde.maildir.instance1") + Query::PropertyFilter("name", "maildir1") + Query::RequestedProperties(QByteArrayList() << "name"))
                          .then<void, KAsync::Job<void>, Folder>([query](const Folder &folder) {
                              return Store::fetchAll<Mail>(Query::PropertyFilter("folder", folder) + Query::RequestedProperties(QByteArrayList() << "folder"
                                                                                                                                                 << "subject"))
                                  .then<void, KAsync::Job<void>, QList<Mail::Ptr>>([query](const QList<Mail::Ptr> &mails) {
                                      ASYNCCOMPARE(mails.size(), 1);
                                      auto mail = mails.first();

                                      return Store::remove(*mail)
                                          .then(ResourceControl::flushReplayQueue(query.resources)) // The change needs to be replayed already
                                          .then(ResourceControl::inspect<Mail>(ResourceControl::Inspection::ExistenceInspection(*mail, false)));
                                  });
                          })
                          .exec();
        result.waitForFinished();
        QVERIFY(!result.errorCode());
    }

    void testMarkMailAsRead()
    {
        using namespace Sink;
        using namespace Sink::ApplicationDomain;

        auto query = Query::ResourceFilter("org.kde.maildir.instance1");
        Store::synchronize(query).exec().waitForFinished();
        ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        Folder f;

        auto result = Store::fetchOne<Folder>(
                          Query::ResourceFilter("org.kde.maildir.instance1") + Query::PropertyFilter("name", "maildir1") + Query::RequestedProperties(QByteArrayList() << "name"))
                          .then<void, KAsync::Job<void>, Folder>([query, &f](const Folder &folder) {
                              f = folder;
                              return Store::fetchAll<Mail>(Query::ResourceFilter("org.kde.maildir.instance1") + Query::PropertyFilter("folder", folder) +
                                                           Query::RequestedProperties(QByteArrayList() << "folder"
                                                                                                       << "subject"))
                                  .then<void, KAsync::Job<void>, QList<Mail::Ptr>>([query](const QList<Mail::Ptr> &mails) {
                                      ASYNCCOMPARE(mails.size(), 1);
                                      auto mail = mails.first();
                                      mail->setProperty("unread", true);
                                      return Store::modify(*mail)
                                          .then<void>(ResourceControl::flushReplayQueue(query.resources)) // The change needs to be replayed already
                                          .then(ResourceControl::inspect<Mail>(ResourceControl::Inspection::PropertyInspection(*mail, "unread", true)))
                                          .then(ResourceControl::inspect<Mail>(ResourceControl::Inspection::PropertyInspection(*mail, "subject", mail->getProperty("subject"))));
                                  });
                          })
                          .exec();
        result.waitForFinished();
        QVERIFY(!result.errorCode());

        // Verify that we can still query for all relevant information
        auto result2 = Store::fetchAll<Mail>(
                           Query::ResourceFilter("org.kde.maildir.instance1") + Query::PropertyFilter("folder", f) + Query::RequestedProperties(QByteArrayList() << "folder"
                                                                                                                                                                 << "subject"
                                                                                                                                                                 << "mimeMessage"
                                                                                                                                                                 << "unread"))
                           .then<void, KAsync::Job<void>, QList<Mail::Ptr>>([](const QList<Mail::Ptr> &mails) {
                               ASYNCCOMPARE(mails.size(), 1);
                               auto mail = mails.first();
                               ASYNCVERIFY(!mail->getProperty("subject").toString().isEmpty());
                               ASYNCCOMPARE(mail->getProperty("unread").toBool(), true);
                               ASYNCVERIFY(QFileInfo(mail->getProperty("mimeMessage").toString()).exists());
                               return KAsync::null<void>();
                           })
                           .exec();
        result2.waitForFinished();
        QVERIFY(!result2.errorCode());
    }

    void testCreateDraft()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";

        Sink::Store::synchronize(query).exec().waitForFinished();
        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto message = KMime::Message::Ptr::create();
        message->subject(true)->fromUnicodeString(QString::fromLatin1("Foobar"), "utf8");
        message->assemble();

        auto mail = Sink::ApplicationDomain::ApplicationDomainType::createEntity<Sink::ApplicationDomain::Mail>("org.kde.maildir.instance1");
        mail.setBlobProperty("mimeMessage", message->encodedContent());
        mail.setProperty("draft", true);

        Sink::Store::create(mail).exec().waitForFinished();

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto future = Sink::Store::fetchOne<ApplicationDomain::Mail>(Query::IdentityFilter(mail.identifier()) + Query::RequestedProperties(QByteArrayList() << "mimeMessage" << "folder"))
            .then<void, ApplicationDomain::Mail>([](const ApplicationDomain::Mail &mail) {
                QVERIFY(!mail.getProperty("folder").toByteArray().isEmpty());
            }).exec();
        future.waitForFinished();
        if (future.errorCode()) {
            qWarning() << future.errorCode() << future.errorMessage();
        }
        QVERIFY(!future.errorCode());
    }
};

QTEST_MAIN(MaildirResourceTest)
#include "maildirresourcetest.moc"
