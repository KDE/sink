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

static QMap<QString, QVariant> createEvent(int i)
{
    QMap<QString, QVariant> event;
    event.insert("summary", QString("summary%1").arg(i));
    static const size_t attachmentSize = 1024*2; // 2KB
    event.insert("attachment", QByteArray(attachmentSize, 'c'));
    return event;
}

static QMap<QString, QVariant> createMail(int i)
{
    QMap<QString, QVariant> mail;
    mail.insert("subject", QString("subject%1").arg(i));
    return mail;
}

static QMap<QString, QVariant> createFolder(int i)
{
    QMap<QString, QVariant> folder;
    folder.insert("name", QString("folder%1").arg(i));
    return folder;
}

QMap<QString, QMap<QString, QVariant> > populateEvents()
{
    QMap<QString, QMap<QString, QVariant>> content;
    for (int i = 0; i < 2; i++) {
        content.insert(QString("key%1").arg(i), createEvent(i));
    }
    return content;
}

QMap<QString, QMap<QString, QVariant> > populateMails()
{
    QMap<QString, QMap<QString, QVariant>> content;
    for (int i = 0; i < 2; i++) {
        content.insert(QString("key%1").arg(i), createMail(i));
    }
    return content;
}

QMap<QString, QMap<QString, QVariant> > populateFolders()
{
    QMap<QString, QMap<QString, QVariant>> content;
    int i = 0;
    for (i = 0; i < 5; i++) {
        content.insert(QString("key%1").arg(i), createFolder(i));
    }
    i++;
    auto folder = createFolder(i);
    folder.insert("parent", "key0");
    content.insert(QString("key%1").arg(i), folder);
    return content;
}

static QMap<QString, QMap<QString, QVariant> > s_eventSource = populateEvents();
static QMap<QString, QMap<QString, QVariant> > s_mailSource = populateMails();
static QMap<QString, QMap<QString, QVariant> > s_folderSource = populateFolders();

QMap<QString, QMap<QString, QVariant> > DummyStore::events() const
{
    return s_eventSource;
}

QMap<QString, QMap<QString, QVariant> > DummyStore::mails() const
{
    return s_mailSource;
}

QMap<QString, QMap<QString, QVariant> > DummyStore::folders() const
{
    return s_folderSource;
}
