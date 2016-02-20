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
#include <functional>
#include "domain/applicationdomaintype.h"
#include "metadata_generated.h"

/*
 * An iterator to a result set.
 *
 * We'll eventually want to lazy load results in next().
 */
class ResultSet {
    public:
        typedef std::function<bool(std::function<void(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &, Sink::Operation)>)> ValueGenerator;
        typedef std::function<QByteArray()> IdGenerator;
        typedef std::function<void()> SkipValue;

        ResultSet();
        ResultSet(const ValueGenerator &generator, const SkipValue &skip);
        ResultSet(const IdGenerator &generator);
        ResultSet(const QVector<QByteArray> &resultSet);
        ResultSet(const ResultSet &other);

        bool next();
        bool next(std::function<bool(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &value, Sink::Operation)> callback);

        void skip(int number);

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

