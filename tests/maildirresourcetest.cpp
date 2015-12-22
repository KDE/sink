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
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(tempDir.isValid());
        auto targetPath = tempDir.path() + "/maildir1/";
        copyRecursively(TESTDATAPATH "/maildir1", targetPath);

        Akonadi2::Log::setDebugOutputLevel(Akonadi2::Log::Trace);
        auto factory = Akonadi2::ResourceFactory::load("org.kde.maildir");
        QVERIFY(factory);
        MaildirResource::removeFromDisk("org.kde.maildir.instance1");
        Akonadi2::ApplicationDomain::AkonadiResource resource;
        resource.setProperty("identifier", "org.kde.maildir.instance1");
        resource.setProperty("type", "org.kde.maildir");
        resource.setProperty("path", targetPath);
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
        QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 2);
    }

    void testListFolderTree()
    {
        Akonadi2::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.syncOnDemand = true;
        query.processAll = true;
        query.parentProperty = "parent";

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 1);
        auto parentIndex = model->index(0, 0, QModelIndex());
        model->fetchMore(parentIndex);
        QTRY_VERIFY(model->data(parentIndex, Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(parentIndex), 1);
    }

    void testListMailsOfFolder()
    {
        {
            Akonadi2::Query query;
            query.resources << "org.kde.maildir.instance1";
            query.syncOnDemand = true;
            query.processAll = true;

            //Ensure all local data is processed
            Akonadi2::Store::synchronize(query).exec().waitForFinished();
        }
        QByteArray folderIdentifier;
        {
            Akonadi2::Query query;
            query.resources << "org.kde.maildir.instance1";
            query.requestedProperties << "folder" << "name";

            auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
            QVERIFY(model->rowCount(QModelIndex()) > 1);
            folderIdentifier = model->index(1, 0, QModelIndex()).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Folder::Ptr>()->identifier();
        }

        Akonadi2::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.requestedProperties << "folder" << "summary";
        query.propertyFilter.insert("folder", folderIdentifier);
        auto mailModel = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(mailModel->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QVERIFY(mailModel->rowCount(QModelIndex()) >= 1);
    }

    void testMailContent()
    {
        Akonadi2::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.requestedProperties << "folder" << "subject" << "mimeMessage" << "date";
        query.syncOnDemand = true;
        query.processAll = true;

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        auto mailModel = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(mailModel->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QVERIFY(mailModel->rowCount(QModelIndex()) >= 1);
        auto mail = mailModel->index(0, 0, QModelIndex()).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Mail::Ptr>();
        QVERIFY(!mail->getProperty("subject").toString().isEmpty());
        QVERIFY(!mail->getProperty("mimeMessage").toString().isEmpty());
        QVERIFY(mail->getProperty("date").toDateTime().isValid());
    }


    void testSyncFolderMove()
    {
        Akonadi2::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.syncOnDemand = true;
        query.processAll = true;
        query.requestedProperties << "name";

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        auto targetPath = tempDir.path() + "/maildir1/";
        QDir dir(targetPath);
        QVERIFY(dir.rename("inbox", "newbox"));

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 2);
        QCOMPARE(model->match(model->index(0, 0, QModelIndex()), Qt::DisplayRole, QStringLiteral("newbox"), 1).size(), 1);
    }

    void testSyncMailMove()
    {
        Akonadi2::Query query;
        query.resources << "org.kde.maildir.instance1";
        query.syncOnDemand = true;
        query.processAll = true;
        query.requestedProperties << "folder" << "summary";

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        auto targetPath = tempDir.path() + QDir::separator() + "maildir1/cur/1365777830.R28.localhost.localdomain:2,S";
        QFile file(targetPath);
        QVERIFY(file.remove());

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        auto mailModel = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(mailModel->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(mailModel->rowCount(QModelIndex()), 1);
    }

};

QTEST_MAIN(MaildirResourceTest)
#include "maildirresourcetest.moc"
