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

#include "test.h"

#include "utils.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

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

    void markAsImportant(const QStringList &folderPath, const QByteArray &messageIdentifier) Q_DECL_OVERRIDE
    {
        auto rootPath = tempDir.path() + "/maildir1/";
        KPIM::Maildir maildir(rootPath + folderPath.join('/'));
        maildir.changeEntryFlags(messageIdentifier, KPIM::Maildir::Flagged);
    }
};

QTEST_MAIN(MaildirMailSyncTest)

#include "maildirmailsynctest.moc"
