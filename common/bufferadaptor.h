/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <QVariant>
#include <QByteArray>
#include <QHash>
#include <QDebug>

namespace Sink {

namespace ApplicationDomain {

/**
 * This class has to be implemented by resources and can be used as generic interface to access the buffer properties
 */
class BufferAdaptor
{
public:
    virtual ~BufferAdaptor()
    {
    }
    virtual QVariant getProperty(const QByteArray &key) const
    {
        qFatal("Tried to get property: " + key);
        return QVariant();
    }
    virtual void setProperty(const QByteArray &key, const QVariant &value)
    {
        qFatal("Tried to get property: " + key);
    }
    virtual QList<QByteArray> availableProperties() const
    {
        return QList<QByteArray>();
    }
};

class MemoryBufferAdaptor : public BufferAdaptor
{
public:
    MemoryBufferAdaptor() : BufferAdaptor()
    {
    }

    MemoryBufferAdaptor(const BufferAdaptor &buffer, const QList<QByteArray> &properties) : BufferAdaptor()
    {
        if (properties.isEmpty()) {
            for (const auto &property : buffer.availableProperties()) {
                mValues.insert(property, buffer.getProperty(property));
            }
        } else {
            for (const auto &property : properties) {
                mValues.insert(property, buffer.getProperty(property));
            }
        }
    }

    virtual ~MemoryBufferAdaptor()
    {
    }

    virtual QVariant getProperty(const QByteArray &key) const
    {
        return mValues.value(key);
    }
    virtual void setProperty(const QByteArray &key, const QVariant &value)
    {
        if (value != mValues.value(key)) {
            mChanges << key;
        }
        mValues.insert(key, value);
    }

    virtual QByteArrayList availableProperties() const
    {
        return mValues.keys();
    }

    void resetChangedProperties()
    {
        mChanges.clear();
    }

    QList<QByteArray> changedProperties() const
    {
        return mChanges;
    }
private:
    QHash<QByteArray, QVariant> mValues;
    QList<QByteArray> mChanges;
};
}
}
