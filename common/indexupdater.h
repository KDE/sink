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

#include <pipeline.h>
#include <index.h>

class IndexUpdater : public Sink::Preprocessor
{
public:
    IndexUpdater(const QByteArray &index, const QByteArray &type, const QByteArray &property) : mIndexIdentifier(index), mBufferType(type), mProperty(property)
    {
    }

    void newEntity(const QByteArray &uid, qint64 revision, Sink::ApplicationDomain::BufferAdaptor &newEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        add(newEntity.getProperty(mProperty), uid, transaction);
    }

    void modifiedEntity(const QByteArray &uid, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::ApplicationDomain::BufferAdaptor &newEntity,
        Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        remove(oldEntity.getProperty(mProperty), uid, transaction);
        add(newEntity.getProperty(mProperty), uid, transaction);
    }

    void deletedEntity(const QByteArray &uid, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        remove(oldEntity.getProperty(mProperty), uid, transaction);
    }

private:
    void add(const QVariant &value, const QByteArray &uid, Sink::Storage::Transaction &transaction)
    {
        if (value.isValid()) {
            Index(mIndexIdentifier, transaction).add(value.toByteArray(), uid);
        }
    }

    void remove(const QVariant &value, const QByteArray &uid, Sink::Storage::Transaction &transaction)
    {
        if (value.isValid()) {
            const auto data = value.toByteArray();
            if (!data.isEmpty()) {
                Index(mIndexIdentifier, transaction).remove(data, uid);
            }
        }
    }

    QByteArray mIndexIdentifier;
    QByteArray mBufferType;
    QByteArray mProperty;
};

template <typename DomainType>
class DefaultIndexUpdater : public Sink::Preprocessor
{
public:
    void newEntity(const QByteArray &uid, qint64 revision, Sink::ApplicationDomain::BufferAdaptor &newEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        Sink::ApplicationDomain::TypeImplementation<DomainType>::index(uid, newEntity, transaction);
    }

    void modifiedEntity(const QByteArray &uid, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::ApplicationDomain::BufferAdaptor &newEntity,
        Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        Sink::ApplicationDomain::TypeImplementation<DomainType>::removeIndex(uid, oldEntity, transaction);
        Sink::ApplicationDomain::TypeImplementation<DomainType>::index(uid, newEntity, transaction);
    }

    void deletedEntity(const QByteArray &uid, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        Sink::ApplicationDomain::TypeImplementation<DomainType>::removeIndex(uid, oldEntity, transaction);
    }
};
