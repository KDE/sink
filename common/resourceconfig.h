/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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

class SINK_EXPORT ResourceConfig
{
public:
    static QMap<QByteArray, QByteArray> getResources();
    static QByteArray newIdentifier(const QByteArray &type);
    static void addResource(const QByteArray &identifier, const QByteArray &type);
    static void removeResource(const QByteArray &identifier);
    static void clear();
    static void configureResource(const QByteArray &identifier, const QMap<QByteArray, QVariant> &configuration);
    static QMap<QByteArray, QVariant> getConfiguration(const QByteArray &identifier);
};
