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

static bool copyRecursively(const QString &srcFilePath,
                            const QString &tgtFilePath)
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
            const QString newSrcFilePath
                    = srcFilePath + QLatin1Char('/') + fileName;
            const QString newTgtFilePath
                    = tgtFilePath + QLatin1Char('/') + fileName;
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
private Q_SLOTS:
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
        Sink::Store::shutdown(QByteArray("org.kde.maildir.instance1")).exec().waitForFinished();
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
        Sink::Store::start(QByteArray("org.kde.maildir.instance1")).exec().waitForFinished();
    }

    void testListFolders()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";

        //Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 2);
    }

    void testListFolderTree()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.parentProperty = "parent";

        //Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 1);
        auto parentIndex = model->index(0, 0, QModelIndex());
        model->fetchMore(parentIndex);
        QTRY_VERIFY(model->data(parentIndex, Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(parentIndex), 1);
    }

    void testListMailsOfFolder()
    {
        using namespace Sink;
        using namespace Sink::ApplicationDomain;
        //Ensure all local data is processed
        auto query = Query::ResourceFilter("org.kde.maildir.instance1");
        Store::synchronize(query).exec().waitForFinished();
        Store::flushMessageQueue(query.resources).exec().waitForFinished();
        auto result = Store::fetchOne<Folder>(
                Query::ResourceFilter("org.kde.maildir.instance1") + Query::RequestedProperties(QByteArrayList() << "name")
            )
            .then<QList<Mail::Ptr>, Folder>([](const Folder &folder) {
                Trace() << "Found a folder" << folder.identifier();
                return Store::fetchAll<Mail>(
                    Query::PropertyFilter("folder", folder) + Query::RequestedProperties(QByteArrayList() << "folder" << "subject")
                );
            })
            .then<void, QList<Mail::Ptr> >([](const QList<Mail::Ptr> &mails) {
                QVERIFY(mails.size() >= 1);
            })
            .exec();
        result.waitForFinished();
        QVERIFY(!result.errorCode());
    }

    void testMailContent()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.requestedProperties << "folder" << "subject" << "mimeMessage" << "date";

        //Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();

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

        //Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();

        auto targetPath = tempDir.path() + "/maildir1/";
        QDir dir(targetPath);
        QVERIFY(dir.rename("inbox", "newbox"));

        //Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 2);
        QCOMPARE(model->match(model->index(0, 0, QModelIndex()), Qt::DisplayRole, QStringLiteral("newbox"), 1).size(), 1);
    }

    void testReSyncMail()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.requestedProperties << "folder" << "subject";

        //Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();

        //Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();

        auto mailModel = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(mailModel->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(mailModel->rowCount(QModelIndex()), 2);
    }

    void testSyncMailRemoval()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.requestedProperties << "folder" << "subject";

        //Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();

        auto targetPath = tempDir.path() + "/maildir1/cur/1365777830.R28.localhost.localdomain:2,S";
        QFile file(targetPath);
        QVERIFY(file.remove());

        //Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();

        auto mailModel = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(mailModel->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(mailModel->rowCount(QModelIndex()), 1);
    }

    void testCreateFolder()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";

        //Ensure all local data is processed
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();

        Sink::ApplicationDomain::Folder folder("org.kde.maildir.instance1");
        folder.setProperty("name", "testCreateFolder");

        Sink::Store::create(folder).exec().waitForFinished();

        //Ensure all local data is processed
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();

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
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();
        QTRY_VERIFY(QFileInfo(targetPath).exists());

        Sink::Query folderQuery;
        folderQuery.resources << "org.kde.maildir.instance1";
        folderQuery.propertyFilter.insert("name", "testCreateFolder");
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(folderQuery);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 1);
        auto createdFolder = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Folder::Ptr>();

        Sink::Store::remove(*createdFolder).exec().waitForFinished();
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();
        QTRY_VERIFY(!QFileInfo(targetPath).exists());
    }

    void testCreateMail()
    {
        Sink::Query query;
        query.resources << "org.kde.maildir.instance1";

        //Ensure all local data is processed
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();

        Sink::ApplicationDomain::Mail mail("org.kde.maildir.instance1");
        mail.setProperty("name", "testCreateMail");

        Sink::Store::create(mail).exec().waitForFinished();

        //Ensure all local data is processed
        Sink::Store::flushMessageQueue(query.resources).exec().waitForFinished();

        auto targetPath = tempDir.path() + "/maildir1/new";
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
        Store::flushMessageQueue(query.resources).exec().waitForFinished();

        auto result = Store::fetchOne<Folder>(
            Query::ResourceFilter("org.kde.maildir.instance1") + Query::PropertyFilter("name", "maildir1") + Query::RequestedProperties(QByteArrayList() << "name")
        )
        .then<void, KAsync::Job<void>, Folder>([query](const Folder &folder) {
            return Store::fetchAll<Mail>(
                Query::PropertyFilter("folder", folder) + Query::RequestedProperties(QByteArrayList() << "folder" << "subject")
            )
            .then<void, KAsync::Job<void>, QList<Mail::Ptr> >([query](const QList<Mail::Ptr> &mails) {
                //Can't use QCOMPARE because it tries to return FIXME Implement ASYNCCOMPARE
                if (mails.size() != 1) {
                    return KAsync::error<void>(1, "Wrong number of mails.");
                }
                auto mail = mails.first();

                return Store::remove(*mail)
                    .then(Store::flushReplayQueue(query.resources)) //The change needs to be replayed already
                    .then(Resources::inspect<Mail>(Resources::Inspection::ExistenceInspection(*mail, false)));
            })
            .then<void>([](){});
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
        Store::flushMessageQueue(query.resources).exec().waitForFinished();

        Folder f;

        auto result = Store::fetchOne<Folder>(
            Query::ResourceFilter("org.kde.maildir.instance1") + Query::PropertyFilter("name", "maildir1") + Query::RequestedProperties(QByteArrayList() << "name")
        )
        .then<void, KAsync::Job<void>, Folder>([query, &f](const Folder &folder) {
            f = folder;
            return Store::fetchAll<Mail>(
                Query::ResourceFilter("org.kde.maildir.instance1") + Query::PropertyFilter("folder", folder) + Query::RequestedProperties(QByteArrayList() << "folder" << "subject")
            )
            .then<void, KAsync::Job<void>, QList<Mail::Ptr> >([query](const QList<Mail::Ptr> &mails) {
                //Can't use QCOMPARE because it tries to return FIXME Implement ASYNCCOMPARE
                if (mails.size() != 1) {
                    return KAsync::error<void>(1, "Wrong number of mails.");
                }
                auto mail = mails.first();
                mail->setProperty("unread", true);
                return Store::modify(*mail)
                .then<void>(Store::flushReplayQueue(query.resources)) //The change needs to be replayed already
                .then(Resources::inspect<Mail>(Resources::Inspection::PropertyInspection(*mail, "unread", true)))
                .then(Resources::inspect<Mail>(Resources::Inspection::PropertyInspection(*mail, "subject", mail->getProperty("subject"))));
            })
            .then<void>([](){});
        })
        .exec();
        result.waitForFinished();
        QVERIFY(!result.errorCode());

        //Verify that we can still query for all relevant information
        auto result2 = Store::fetchAll<Mail>(
            Query::ResourceFilter("org.kde.maildir.instance1") + Query::PropertyFilter("folder", f) + Query::RequestedProperties(QByteArrayList() << "folder" << "subject" << "mimeMessage" << "unread")
        )
        .then<void, KAsync::Job<void>, QList<Mail::Ptr> >([](const QList<Mail::Ptr> &mails) {
            //Can't use QCOMPARE because it tries to return FIXME Implement ASYNCCOMPARE
            if (mails.size() != 1) {
                qWarning() << "Wrong number of mails";
                return KAsync::error<void>(1, "Wrong number of mails.");
            }
            auto mail = mails.first();
            if (mail->getProperty("subject").toString().isEmpty()) {
                qWarning() << "Wrong subject";
                return KAsync::error<void>(1, "Wrong subject.");
            }
            if (mail->getProperty("unread").toBool() != true) {
                qWarning() << "Not unread";
                return KAsync::error<void>(1, "Not unread.");
            }
            QFileInfo info(mail->getProperty("mimeMessage").toString());
            if (!info.exists()) {
                qWarning() << "Wrong subject";
                return KAsync::error<void>(1, "Can't find mime message.");
            }
            return KAsync::null<void>();
        })
        .exec();
        result2.waitForFinished();
        QVERIFY(!result2.errorCode());
    }

};

QTEST_MAIN(MaildirResourceTest)
#include "maildirresourcetest.moc"
