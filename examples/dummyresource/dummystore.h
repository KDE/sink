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
#pragma once
#include <QMap>
#include <QVariant>

class DummyStore
{
public:
    static DummyStore &instance()
    {
        static DummyStore instance;
        return instance;
    }

    void populate();

    QMap<QString, QMap<QString, QVariant> > &events();
    QMap<QString, QMap<QString, QVariant> > &mails();
    QMap<QString, QMap<QString, QVariant> > &folders();

private:
    DummyStore();

    QMap<QString, QMap<QString, QVariant> > populateEvents();
    QMap<QString, QMap<QString, QVariant> > populateMails();
    QMap<QString, QMap<QString, QVariant> > populateFolders();

    QMap<QString, QMap<QString, QVariant> > mEvents;
    QMap<QString, QMap<QString, QVariant> > mMails;
    QMap<QString, QMap<QString, QVariant> > mFolders;
};
