#include <QtTest>
#include <QString>
#include <iostream>

#include "hawd/dataset.h"
#include "hawd/formatter.h"

#include "maildirresource/maildirresource.h"
#include "store.h"
#include "resourcecontrol.h"
#include "commands.h"
#include "entitybuffer.h"
#include "resourceconfig.h"
#include "modelresult.h"
#include "pipeline.h"
#include "log.h"


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
            qWarning() << "Failed to copy file " << srcFilePath << tgtFilePath;
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
class MaildirSyncBenchmark : public QObject
{
    Q_OBJECT

    QTemporaryDir tempDir;
    QString targetPath;
    HAWD::State mHawdState;

private slots:
    void initTestCase()
    {
        targetPath = tempDir.path() + "/maildir1";

        MaildirResource::removeFromDisk("sink.maildir.test1");
        Sink::ApplicationDomain::SinkResource resource;
        resource.setProperty("identifier", "sink.maildir.test1");
        resource.setProperty("type", "sink.maildir");
        resource.setProperty("path", targetPath);
        Sink::Store::create(resource).exec().waitForFinished();
    }

    void cleanup()
    {
        MaildirResource::removeFromDisk("sink.maildir.test1");
        QDir dir(targetPath);
        dir.removeRecursively();
    }

    void init()
    {
        copyRecursively(TESTDATAPATH "/maildir1", targetPath);
    }

    void testbench()
    {
        auto pipeline = QSharedPointer<Sink::Pipeline>::create("sink.maildir.test1");
        MaildirResource resource("sink.maildir.test1", pipeline);
        QTime time;
        time.start();
        resource.Sink::GenericResource::synchronizeWithSource(Sink::QueryBase()).exec().waitForFinished();
        std::cout << "Sync took " << time.elapsed() << std::endl;
        resource.processAllMessages().exec().waitForFinished();
        const auto allProcessedTime = time.elapsed();
        std::cout << "All done " << allProcessedTime << std::endl;

        // HAWD::Dataset dataset("maildir_sync", mHawdState);
        // HAWD::Dataset::Row row = dataset.row();
        // row.setValue("totalTime", allProcessedTime);
        // dataset.insertRow(row);
        // HAWD::Formatter::print(dataset);
    }
};

QTEST_MAIN(MaildirSyncBenchmark)
#include "maildirsyncbenchmark.moc"
