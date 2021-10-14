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
#pragma once

#include "sinktest_export.h"
#include <QObject>
#include <QByteArray>

#include <applicationdomaintype.h>
#include "test.h"

namespace Sink {

/**
 * Tests if the resource can synchronize (read-only) emails.
 * 
 * The default testenvironment is:
 * * INBOX
 * * INBOX.test
 */
class SINKTEST_EXPORT MailSyncTest : public QObject
{
    Q_OBJECT

protected:
    QByteArray mResourceInstanceIdentifier;
    QByteArrayList mCapabilities;

    virtual bool isBackendAvailable() { return true; }
    virtual void resetTestEnvironment() = 0;
    virtual Sink::ApplicationDomain::SinkResource createResource() = 0;
    virtual Sink::ApplicationDomain::SinkResource createFaultyResource() = 0;
    virtual void removeResourceFromDisk(const QByteArray &mResourceInstanceIdentifier) = 0;
    virtual void createFolder(const QStringList &folderPath) = 0;
    virtual void removeFolder(const QStringList &folderPath) = 0;
    virtual QByteArray createMessage(const QStringList &folderPath, const QByteArray &message) = 0;
    virtual void removeMessage(const QStringList &folderPath, const QByteArray &messageIdentifier) = 0;
    virtual void markAsImportant(const QStringList &folderPath, const QByteArray &messageIdentifier) = 0;

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void testListFolders();
    void testListNewFolder();
    void testListRemovedFolder();
    void testListFolderHierarchy();
    void testListNewSubFolder();
    void testListRemovedSubFolder();
    void testListRemovedFullFolder();

    void testListMails();
    void testResyncMails();
    void testFetchNewRemovedMessages();
    void testFlagChange();

    void testSyncSingleFolder();
    void testSyncSingleMail();
    void testSyncSingleMailWithBogusId();

    void testFailingSync();
    void testSyncUidvalidity();
};

}

