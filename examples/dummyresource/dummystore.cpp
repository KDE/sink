/*
 *   Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include "dummystore.h"

#include <QString>
#include <QDateTime>
#include <QUuid>

static QMap<QString, QVariant> createEvent(int i)
{
    QMap<QString, QVariant> event;
    event.insert("summary", QString("summary%1").arg(i));
    static const size_t attachmentSize = 1024*2; // 2KB
    event.insert("attachment", QByteArray(attachmentSize, 'c'));
    return event;
}

QMap<QString, QMap<QString, QVariant> > DummyStore::populateEvents()
{
    QMap<QString, QMap<QString, QVariant>> content;
    for (int i = 0; i < 2; i++) {
        content.insert(QString("key%1").arg(i), createEvent(i));
    }
    return content;
}

static QByteArray addMail(QMap <QString, QMap<QString, QVariant> > &content, const QString &subject, const QDateTime &date, const QString &senderName, const QString &senderEmail, bool isUnread, bool isImportant, const QByteArray &parentFolder)
{
    static int id = 0;
    id++;
    const auto uid = QString("key%1").arg(id);
    QMap<QString, QVariant> mail;
    mail.insert("subject", subject);
    mail.insert("date", date);
    mail.insert("senderName", senderName);
    mail.insert("senderEmail", senderEmail);
    mail.insert("unread", isUnread);
    mail.insert("important", isImportant);
    mail.insert("parentFolder", parentFolder);
    content.insert(uid, mail);
    return uid.toUtf8();
}

QMap<QString, QMap<QString, QVariant> > DummyStore::populateMails()
{
    QMap<QString, QMap<QString, QVariant>> content;
    for (const auto &parentFolder : mFolders.keys()) {
        addMail(content, "Hello World! " + QUuid::createUuid().toByteArray(), QDateTime::currentDateTimeUtc(), "John Doe", "doe@example.com", true, false, parentFolder.toUtf8());
    }
    return content;
}

static QByteArray addFolder(QMap <QString, QMap<QString, QVariant> > &content, const QString &name, const QByteArray &icon, const QByteArray &parent = QByteArray())
{
    static int id = 0;
    id++;
    const auto uid = QString("key%1").arg(id);
    QMap<QString, QVariant> folder;
    folder.insert("name", name);
    if (!parent.isEmpty()) {
        folder.insert("parent", parent);
    }
    folder.insert("icon", icon);
    content.insert(uid, folder);
    return uid.toUtf8();
}

QMap<QString, QMap<QString, QVariant> > DummyStore::populateFolders()
{
    QMap<QString, QMap<QString, QVariant>> content;
    addFolder(content, "Inbox", "mail-folder-inbox");
    auto data = addFolder(content, "Data", "folder");
    addFolder(content, "Sent", "mail-folder-sent");
    addFolder(content, "Trash", "user-trash");
    addFolder(content, "Drafts", "document-edit");
    addFolder(content, "Stuff", "folder", data);
    auto bulk = addFolder(content, "Bulk", "folder", data);
    for (int i = 0; i < 5; i++) {
        addFolder(content, QString("Folder %1").arg(i), "folder", bulk);
    }
    return content;
}

DummyStore::DummyStore()
{
}

void DummyStore::populate()
{
    mFolders = populateFolders();
    mMails = populateMails();
    mEvents = populateEvents();
}

QMap<QString, QMap<QString, QVariant> > &DummyStore::events()
{
    return mEvents;
}

QMap<QString, QMap<QString, QVariant> > &DummyStore::mails()
{
    return mMails;
}

QMap<QString, QMap<QString, QVariant> > &DummyStore::folders()
{
    return mFolders;
}
