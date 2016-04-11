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

#include "sink_export.h"
#include <QList>
#include <QByteArray>
#include <QVariant>
#include <QMap>
#include <QSettings>
#include <QSharedPointer>

class SINK_EXPORT ConfigStore
{
public:
    ConfigStore(const QByteArray &identifier);

    /**
     * Returns all entries with their type.
     */
    QMap<QByteArray, QByteArray> getEntries();

    /**
     * Create an entry with a type.
     */
    void add(const QByteArray &identifier, const QByteArray &type);

    /**
     * Remove an entry.
     */
    void remove(const QByteArray &identifier);

    /**
     * Remove all entries
     */
    void clear();

    /**
     * Modify the configuration of an entry.
     */
    void modify(const QByteArray &identifier, const QMap<QByteArray, QVariant> &configuration);

    /**
     * Get the configuration of an entry.
     */
    QMap<QByteArray, QVariant> get(const QByteArray &identifier);

private:
    QByteArray mIdentifier;
    QSharedPointer<QSettings> mConfig;
};
