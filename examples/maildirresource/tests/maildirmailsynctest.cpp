/*
 *   Copyright (C) 2016 Christian Mollekopf <chrigi_1@fastmail.fm>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */
#include <QtTest>

#include <tests/mailsynctest.h>
#include "../maildirresource.h"
#include "../libmaildir/maildir.h"

#include "common/test.h"
#include "common/domain/applicationdomaintype.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

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
class MaildirMailSyncTest : public Sink::MailSyncTest
{
    Q_OBJECT

    QTemporaryDir tempDir;
    QString targetPath;

protected:
    void resetTestEnvironment() Q_DECL_OVERRIDE
    {
        targetPath = tempDir.path() + "/maildir1/";
        QDir dir(targetPath);
        dir.removeRecursively();
        copyRecursively(TESTDATAPATH "/maildir1", targetPath);
    }

    Sink::ApplicationDomain::SinkResource createResource() Q_DECL_OVERRIDE
    {
        auto resource = ApplicationDomain::MaildirResource::create("account1");
        resource.setProperty("path", targetPath);
        return resource;
    }

    Sink::ApplicationDomain::SinkResource createFaultyResource() Q_DECL_OVERRIDE
    {
        auto resource = ApplicationDomain::MaildirResource::create("account1");
        resource.setProperty("path", "");
        return resource;
    }

    void removeResourceFromDisk(const QByteArray &identifier) Q_DECL_OVERRIDE
    {
        ::MaildirResource::removeFromDisk(identifier);
    }

    void createFolder(const QStringList &folderPath) Q_DECL_OVERRIDE
    {
        auto rootPath = tempDir.path() + "/maildir1/";
        KPIM::Maildir maildir(rootPath + folderPath.join('/'), false);
        maildir.create();
    }

    void removeFolder(const QStringList &folderPath) Q_DECL_OVERRIDE
    {
        auto rootPath = tempDir.path() + "/maildir1/";
        KPIM::Maildir maildir(rootPath + folderPath.join('/'), false);
        maildir.remove();
        QDir dir(rootPath + folderPath.join('/'));
        dir.removeRecursively();
        // QVERIFY(maildir.removeSubFolder(name));
    }

    QByteArray createMessage(const QStringList &folderPath, const QByteArray &message) Q_DECL_OVERRIDE
    {
        auto rootPath = tempDir.path() + "/maildir1/";
        KPIM::Maildir maildir(rootPath + folderPath.join('/'));
        return maildir.addEntry(message).toUtf8();
    }

    void removeMessage(const QStringList &folderPath, const QByteArray &messageIdentifier) Q_DECL_OVERRIDE
    {
        auto rootPath = tempDir.path() + "/maildir1/";
        KPIM::Maildir maildir(rootPath + folderPath.join('/'));
        maildir.removeEntry(messageIdentifier);
    }
};

QTEST_MAIN(MaildirMailSyncTest)

#include "maildirmailsynctest.moc"
