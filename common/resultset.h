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

#include <QVector>
#include <QMap>
#include <QVariant>
#include <functional>
#include "metadata_generated.h"
#include "entitybuffer.h"
#include "applicationdomaintype.h"

/*
 * An iterator to a result set.
 *
 * We'll eventually want to lazy load results in next().
 */
class ResultSet
{
public:
    struct Result {
        Result(const Sink::ApplicationDomain::ApplicationDomainType &e, Sink::Operation op, const QMap<QByteArray, QVariant> &v = {}, const QVector<QByteArray> &a = {}) : entity(e), operation(op), aggregateValues(v), aggregateIds(a) {}
        Sink::ApplicationDomain::ApplicationDomainType entity;
        Sink::Operation operation;
        QMap<QByteArray, QVariant> aggregateValues;
        QVector<QByteArray> aggregateIds;
    };
    typedef std::function<void(const Result &)> Callback;
    typedef std::function<bool(Callback)> ValueGenerator;
    typedef std::function<QByteArray()> IdGenerator;
    typedef std::function<void()> SkipValue;

    ResultSet();
    ResultSet(const ValueGenerator &generator, const SkipValue &skip);
    ResultSet(const IdGenerator &generator);
    ResultSet(const QVector<QByteArray> &resultSet);
    ResultSet(const ResultSet &other);

    bool next();
    bool next(const Callback &callback);

    void skip(int number);

    struct ReplayResult {
        qint64 replayedEntities;
        bool replayedAll;
    };
    ReplayResult replaySet(int offset, int batchSize, const Callback &callback);

    QByteArray id();

    bool isEmpty();

private:
    QVector<QByteArray> mResultSet;
    QVector<QByteArray>::ConstIterator mIt;
    QByteArray mCurrentValue;
    IdGenerator mGenerator;
    ValueGenerator mValueGenerator;
    SkipValue mSkip;
    bool mFirst;
};
