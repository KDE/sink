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

/*
 * An iterator to a result set.
 *
 * We'll eventually want to lazy load results in next().
 */
class ResultSet {
    public:


        ResultSet(const std::function<bool(std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &)>)> &generator)
            : mValueGenerator(generator),
            mIt(nullptr)
        {

        }

        ResultSet(const std::function<QByteArray()> &generator)
            : mGenerator(generator),
            mIt(nullptr)
        {

        }

        ResultSet(const QVector<QByteArray> &resultSet)
            : mResultSet(resultSet),
            mIt(nullptr)
        {

        }

        bool next()
        {
            if (mGenerator) {
                mCurrentValue = mGenerator();
            } else {
                if (!mIt) {
                    mIt = mResultSet.constBegin();
                } else {
                    mIt++;
                }
                return mIt != mResultSet.constEnd();
            }
        }

        bool next(std::function<bool(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &value)> callback)
        {
            Q_ASSERT(mValueGenerator);
            return mValueGenerator(callback);
        }

        bool next(std::function<void(const QByteArray &key)> callback)
        {
            if (mGenerator) {
                mCurrentValue = mGenerator();
            } else {
                if (!mIt) {
                    mIt = mResultSet.constBegin();
                } else {
                    mIt++;
                }
                return mIt != mResultSet.constEnd();
            }

        }

        QByteArray id()
        {
            if (mIt) {
                return *mIt;
            } else {
                return mCurrentValue;
            }
        }

        bool isEmpty()
        {
            return mResultSet.isEmpty();
        }

    private:
        QVector<QByteArray> mResultSet;
        QVector<QByteArray>::ConstIterator mIt;
        QByteArray mCurrentValue;
        std::function<QByteArray()> mGenerator;
        std::function<bool(std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &)>)> mValueGenerator;
};

