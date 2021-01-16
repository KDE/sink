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
#include <QVariant>
#include <QByteArray>
#include <functional>
#include <flatbuffers/flatbuffers.h>

namespace Sink {
namespace ApplicationDomain {
namespace Buffer {
    struct MailContact;
    struct ContactEmail;
}
}
}

/**
 * Defines how to convert qt primitives to flatbuffer ones
 */
template <class T>
flatbuffers::uoffset_t SINK_EXPORT variantToProperty(const QVariant &, flatbuffers::FlatBufferBuilder &fbb);

/**
 * Defines how to convert flatbuffer primitives to qt ones
 */
template <typename T>
QVariant SINK_EXPORT propertyToVariant(const flatbuffers::String *);
template <typename T>
QVariant SINK_EXPORT propertyToVariant(uint8_t);
template <typename T>
QVariant SINK_EXPORT propertyToVariant(int);
template <typename T>
QVariant SINK_EXPORT propertyToVariant(const flatbuffers::Vector<uint8_t> *);
template <typename T>
QVariant SINK_EXPORT propertyToVariant(const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *);
template <typename T>
QVariant SINK_EXPORT propertyToVariant(const Sink::ApplicationDomain::Buffer::MailContact *);
template <typename T>
QVariant SINK_EXPORT propertyToVariant(const flatbuffers::Vector<flatbuffers::Offset<Sink::ApplicationDomain::Buffer::MailContact>> *);
template <typename T>
QVariant SINK_EXPORT propertyToVariant(const flatbuffers::Vector<flatbuffers::Offset<Sink::ApplicationDomain::Buffer::ContactEmail>> *);

/**
 * The property mapper is a non-typesafe virtual dispatch.
 *
 * Instead of using an interface and requring each implementation to override
 * a virtual method per property, the property mapper can be filled with accessors
 * that extract the properties from resource types.
 */
class PropertyMapper
{
public:
    virtual ~PropertyMapper(){};

    template <typename T, typename Buffer, typename BufferBuilder, typename FunctionReturnValue, typename Arg>
    void addMapping(FunctionReturnValue (Buffer::*f)() const, void (BufferBuilder::*f2)(Arg))
    {
        addReadMapping<T, Buffer, FunctionReturnValue>(f);
        addWriteMapping<T, BufferBuilder>(f2);
    }

    virtual QVariant getProperty(const QByteArray &key, void const *buffer) const
    {
        const auto it = mReadAccessors.constFind(key);
        if (it != mReadAccessors.end()) {
            const auto accessor = it.value();
            return accessor(buffer);
        }
        return QVariant();
    }

    virtual void setProperty(const QByteArray &key, const QVariant &value, QList<std::function<void(void *builder)>> &builderCalls, flatbuffers::FlatBufferBuilder &fbb) const
    {
        const auto it = mWriteAccessors.constFind(key);
        if (it != mWriteAccessors.end()) {
            const auto accessor = it.value();
            builderCalls << accessor(value, fbb);
        }
    }

    bool hasMapping(const QByteArray &key) const
    {
        return mReadAccessors.contains(key);
    }

    QList<QByteArray> availableProperties() const
    {
        return mReadAccessors.keys();
    }

private:
    void addReadMapping(const QByteArray &property, const std::function<QVariant(void const *)> &mapping)
    {
        mReadAccessors.insert(property, mapping);
    }

    template <typename T, typename Buffer, typename FunctionReturnValue>
    void addReadMapping(FunctionReturnValue (Buffer::*f)() const)
    {
        addReadMapping(T::name, [f](void const *buffer) -> QVariant { return propertyToVariant<typename T::Type>((static_cast<const Buffer*>(buffer)->*f)()); });
    }


    void addWriteMapping(const QByteArray &property, const std::function<std::function<void(void *builder)>(const QVariant &, flatbuffers::FlatBufferBuilder &)> &mapping)
    {
        mWriteAccessors.insert(property, mapping);
    }

    template <typename T, typename BufferBuilder>
    void addWriteMapping(void (BufferBuilder::*f)(uint8_t))
    {
        addWriteMapping(T::name, [f](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(void *builder)> {
            return [value, f](void *builder) { (static_cast<BufferBuilder*>(builder)->*f)(value.value<typename T::Type>()); };
        });
    }

    template <typename T, typename BufferBuilder>
    void addWriteMapping(void (BufferBuilder::*f)(int))
    {
        addWriteMapping(T::name, [f](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(void *builder)> {
            return [value, f](void *builder) { (static_cast<BufferBuilder*>(builder)->*f)(value.value<typename T::Type>()); };
        });
    }

    template <typename T, typename BufferBuilder>
    void addWriteMapping(void (BufferBuilder::*f)(bool))
    {
        addWriteMapping(T::name, [f](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(void *builder)> {
            return [value, f](void *builder) { (static_cast<BufferBuilder*>(builder)->*f)(value.value<typename T::Type>()); };
        });
    }

    template <typename T, typename BufferBuilder, typename Arg>
    void addWriteMapping(void (BufferBuilder::*f)(flatbuffers::Offset<Arg>))
    {
        addWriteMapping(T::name, [f](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(void *builder)> {
            auto offset = variantToProperty<typename T::Type>(value, fbb);
            return [offset, f](void *builder) { (static_cast<BufferBuilder*>(builder)->*f)(offset); };
        });
    }

    QHash<QByteArray, std::function<QVariant(void const *)>> mReadAccessors;
    QHash<QByteArray, std::function<std::function<void(void *builder)>(const QVariant &, flatbuffers::FlatBufferBuilder &)>> mWriteAccessors;
};

