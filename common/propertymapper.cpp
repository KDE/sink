/*
 * Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "propertymapper.h"
#include <QDateTime>

template <>
flatbuffers::uoffset_t variantToProperty<QString>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        return fbb.CreateString(property.toString().toStdString()).o;
    }
    return 0;
}

template <>
flatbuffers::uoffset_t variantToProperty<QByteArray>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        return fbb.CreateString(property.toByteArray().toStdString()).o;
    }
    return 0;
}

template <>
flatbuffers::uoffset_t variantToProperty<QDateTime>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        return fbb.CreateString(property.toDateTime().toString().toStdString()).o;
    }
    return 0;
}

template <>
flatbuffers::uoffset_t variantToProperty<QByteArrayList>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        const auto list = property.value<QByteArrayList>();
        std::vector<flatbuffers::Offset<flatbuffers::String>> vector;
        for (const auto value : list) {
            auto offset = fbb.CreateString(value.toStdString());
            vector.push_back(offset);
        }
        return fbb.CreateVector(vector).o;
    }
    return 0;
}

template <>
QVariant propertyToVariant<QString>(const flatbuffers::String *property)
{
    if (property) {
        // We have to copy the memory, otherwise it would become eventually invalid
        return QString::fromStdString(property->c_str());
    }
    return QVariant();
}

template <>
QVariant propertyToVariant<QByteArray>(const flatbuffers::String *property)
{
    if (property) {
        // We have to copy the memory, otherwise it would become eventually invalid
        return QString::fromStdString(property->c_str()).toUtf8();
    }
    return QVariant();
}

template <>
QVariant propertyToVariant<QByteArray>(const flatbuffers::Vector<uint8_t> *property)
{
    if (property) {
        // We have to copy the memory, otherwise it would become eventually invalid
        return QByteArray(reinterpret_cast<const char *>(property->Data()), property->Length());
    }
    return QVariant();
}

template <>
QVariant propertyToVariant<QByteArrayList>(const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *property)
{
    if (property) {
        QByteArrayList list;
        for (auto it = property->begin(); it != property->end();) {
            // We have to copy the memory, otherwise it would become eventually invalid
            list << QString::fromStdString((*it)->c_str()).toUtf8();
            it.operator++();
        }
        return QVariant::fromValue(list);
    }
    return QVariant();
}

template <>
QVariant propertyToVariant<bool>(uint8_t property)
{
    return static_cast<bool>(property);
}

template <>
QVariant propertyToVariant<QDateTime>(const flatbuffers::String *property)
{
    if (property) {
        // We have to copy the memory, otherwise it would become eventually invalid
        return QDateTime::fromString(QString::fromStdString(property->c_str()));
    }
    return QVariant();
}
