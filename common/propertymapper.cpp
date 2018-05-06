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

#include "applicationdomaintype.h"
#include <QDateTime>
#include <QDataStream>
#include "mail_generated.h"
#include "contact_generated.h"

template <>
flatbuffers::uoffset_t variantToProperty<QString>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        return fbb.CreateString(property.toString().toStdString()).o;
    }
    return 0;
}

template <>
flatbuffers::uoffset_t variantToProperty<Sink::ApplicationDomain::Reference>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        return fbb.CreateString(property.value<Sink::ApplicationDomain::Reference>().value.toStdString()).o;
    }
    return 0;
}

template <>
flatbuffers::uoffset_t variantToProperty<QByteArray>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        const auto ba = property.toByteArray();
        const auto s = fbb.CreateString(ba.constData(), ba.size());
        return s.o;
    }
    return 0;
}

template <>
flatbuffers::uoffset_t variantToProperty<QDateTime>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        QByteArray ba;
        QDataStream ds(&ba, QIODevice::WriteOnly);
        ds << property.toDateTime();
        return fbb.CreateString(ba.toStdString()).o;
    }
    return 0;
}

template <>
flatbuffers::uoffset_t variantToProperty<QByteArrayList>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        const auto list = property.value<QByteArrayList>();
        std::vector<flatbuffers::Offset<flatbuffers::String>> vector;
        for (const auto &value : list) {
            auto offset = fbb.CreateString(value.toStdString());
            vector.push_back(offset);
        }
        return fbb.CreateVector(vector).o;
    }
    return 0;
}

template <>
flatbuffers::uoffset_t variantToProperty<QStringList>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        const auto list = property.value<QStringList>();
        std::vector<flatbuffers::Offset<flatbuffers::String>> vector;
        for (const auto &value : list) {
            auto offset = fbb.CreateString(value.toStdString());
            vector.push_back(offset);
        }
        return fbb.CreateVector(vector).o;
    }
    return 0;
}

template <>
flatbuffers::uoffset_t variantToProperty<Sink::ApplicationDomain::Mail::Contact>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        const auto value = property.value<Sink::ApplicationDomain::Mail::Contact>();
        return Sink::ApplicationDomain::Buffer::CreateMailContactDirect(fbb, value.name.toUtf8().constData(), value.emailAddress.toUtf8().constData()).o;
    }
    return 0;
}

template <>
flatbuffers::uoffset_t variantToProperty<QList<Sink::ApplicationDomain::Mail::Contact>>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        const auto list = property.value<QList<Sink::ApplicationDomain::Mail::Contact>>();
        std::vector<flatbuffers::Offset<Sink::ApplicationDomain::Buffer::MailContact>> vector;
        for (const auto &value : list) {
            auto offset = Sink::ApplicationDomain::Buffer::CreateMailContactDirect(fbb, value.name.toUtf8().constData(), value.emailAddress.toUtf8().constData()).o;
            vector.push_back(offset);
        }
        return fbb.CreateVector(vector).o;
    }
    return 0;
}

template <>
flatbuffers::uoffset_t variantToProperty<QList<Sink::ApplicationDomain::Contact::Email>>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        const auto list = property.value<QList<Sink::ApplicationDomain::Contact::Email>>();
        std::vector<flatbuffers::Offset<Sink::ApplicationDomain::Buffer::ContactEmail>> vector;
        for (const auto &value : list) {
            auto offset = Sink::ApplicationDomain::Buffer::CreateContactEmailDirect(fbb, value.type, value.email.toUtf8().constData()).o;
            vector.push_back(offset);
        }
        return fbb.CreateVector(vector).o;
    }
    return 0;
}


QString propertyToString(const flatbuffers::String *property)
{
    if (property) {
        // We have to copy the memory, otherwise it would become eventually invalid
        return QString::fromStdString(property->str());
    }
    return QString();
}

template <>
QVariant propertyToVariant<QString>(const flatbuffers::String *property)
{
    if (property) {
        // We have to copy the memory, otherwise it would become eventually invalid
        return QString::fromStdString(property->str());
    }
    return QVariant();
}

template <>
QVariant propertyToVariant<Sink::ApplicationDomain::Reference>(const flatbuffers::String *property)
{
    if (property) {
        // We have to copy the memory, otherwise it would become eventually invalid
        return QVariant::fromValue(Sink::ApplicationDomain::Reference{QString::fromStdString(property->str()).toUtf8()});
    }
    return QVariant();
}

template <>
QVariant propertyToVariant<QByteArray>(const flatbuffers::String *property)
{
    if (property) {
        // We have to copy the memory, otherwise it would become eventually invalid
        return QByteArray(property->c_str(), property->Length());
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
            list << QString::fromStdString((*it)->str()).toUtf8();
            it.operator++();
        }
        return QVariant::fromValue(list);
    }
    return QVariant();
}

template <>
QVariant propertyToVariant<QStringList>(const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *property)
{
    if (property) {
        QStringList list;
        for (auto it = property->begin(); it != property->end();) {
            // We have to copy the memory, otherwise it would become eventually invalid
            list << QString::fromStdString((*it)->str());
            it.operator++();
        }
        return QVariant::fromValue(list);
    }
    return QVariant();
}

template <>
QVariant propertyToVariant<Sink::ApplicationDomain::Mail::Contact>(const Sink::ApplicationDomain::Buffer::MailContact *property)
{
    if (property) {
        return QVariant::fromValue(Sink::ApplicationDomain::Mail::Contact{propertyToString(property->name()), propertyToString(property->email())});
    }
    return QVariant();

}

template <>
QVariant propertyToVariant<QList<Sink::ApplicationDomain::Mail::Contact>>(const flatbuffers::Vector<flatbuffers::Offset<Sink::ApplicationDomain::Buffer::MailContact>> *property)
{
    if (property) {
        QList<Sink::ApplicationDomain::Mail::Contact> list;
        for (auto it = property->begin(); it != property->end();) {
            // We have to copy the memory, otherwise it would become eventually invalid
            list << Sink::ApplicationDomain::Mail::Contact{propertyToString(it->name()), propertyToString(it->email())};
            it.operator++();
        }
        return QVariant::fromValue(list);
    }
    return QVariant();
}

template <>
QVariant propertyToVariant<QList<Sink::ApplicationDomain::Contact::Email>>(const flatbuffers::Vector<flatbuffers::Offset<Sink::ApplicationDomain::Buffer::ContactEmail>> *property)
{
    if (property) {
        QList<Sink::ApplicationDomain::Contact::Email> list;
        for (auto it = property->begin(); it != property->end();) {
            list << Sink::ApplicationDomain::Contact::Email{static_cast<Sink::ApplicationDomain::Contact::Email::Type>(it->type()), propertyToString(it->email())};
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
QVariant propertyToVariant<int>(uint8_t property)
{
    return static_cast<int>(property);
}

template <>
QVariant propertyToVariant<QDateTime>(const flatbuffers::String *property)
{
    if (property) {
        auto ba = QByteArray::fromRawData(property->c_str(), property->size());
        QDateTime dt;
        QDataStream ds(&ba, QIODevice::ReadOnly);
        ds >> dt;
        return dt;
    }
    return QVariant();
}
