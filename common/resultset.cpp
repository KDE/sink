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
#include "resultset.h"

#include "log.h"

ResultSet::ResultSet() : mIt(nullptr)
{
}

ResultSet::ResultSet(const ValueGenerator &generator, const SkipValue &skip) : mIt(nullptr), mValueGenerator(generator), mSkip(skip)
{
}

ResultSet::ResultSet(const IdGenerator &generator) : mIt(nullptr), mGenerator(generator), mSkip([this]() { next(); })
{
}

ResultSet::ResultSet(const QVector<QByteArray> &resultSet)
    : mResultSet(resultSet),
      mIt(mResultSet.constBegin()),
      mSkip([this]() {
          if (mIt != mResultSet.constEnd()) {
              mIt++;
          }
      }),
      mFirst(true)
{
}

ResultSet::ResultSet(const ResultSet &other) : mResultSet(other.mResultSet), mIt(nullptr), mFirst(true)
{
    if (other.mValueGenerator) {
        mValueGenerator = other.mValueGenerator;
        mSkip = other.mSkip;
    } else if (other.mGenerator) {
        mGenerator = other.mGenerator;
        mSkip = [this]() { next(); };
    } else {
        mResultSet = other.mResultSet;
        mIt = mResultSet.constBegin();
        mSkip = [this]() {
            if (mIt != mResultSet.constEnd()) {
                mIt++;
            }
        };
    }
}

bool ResultSet::next()
{
    Q_ASSERT(!mValueGenerator);
    if (mIt) {
        if (mIt != mResultSet.constEnd() && !mFirst) {
            mIt++;
        }
        mFirst = false;
        return mIt != mResultSet.constEnd();
    } else if (mGenerator) {
        Q_ASSERT(mGenerator);
        mCurrentValue = mGenerator();
        if (!mCurrentValue.isNull()) {
            return true;
        }
    } else {
        next([](const Result &) { return false; });
    }
    return false;
}

bool ResultSet::next(const Callback &callback)
{
    Q_ASSERT(mValueGenerator);
    return mValueGenerator(callback);
}

void ResultSet::skip(int number)
{
    Q_ASSERT(mSkip);
    for (int i = 0; i < number; i++) {
        mSkip();
    }
}

ResultSet::ReplayResult ResultSet::replaySet(int offset, int batchSize, const Callback &callback)
{
    skip(offset);
    int counter = 0;
    while (!batchSize || (counter < batchSize)) {
        const bool ret = next([this, &counter, callback](const ResultSet::Result &result) {
                counter++;
                callback(result);
            });
        if (!ret) {
            return {counter, true};
        }
    };
    return {counter, false};
}

QByteArray ResultSet::id()
{
    if (mIt) {
        if (mIt == mResultSet.constEnd()) {
            return QByteArray();
        }
        Q_ASSERT(mIt != mResultSet.constEnd());
        return *mIt;
    } else {
        return mCurrentValue;
    }
}

bool ResultSet::isEmpty()
{
    return mResultSet.isEmpty();
}
