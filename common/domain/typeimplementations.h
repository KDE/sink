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

#include "sink_export.h"
#include "applicationdomaintype.h"

#include "mail_generated.h"
#include "folder_generated.h"
#include "event_generated.h"
#include "todo_generated.h"
#include "calendar_generated.h"
#include "contact_generated.h"
#include "addressbook_generated.h"

class PropertyMapper;
class IndexPropertyMapper;

class TypeIndex;

/**
 * Implements all type-specific code such as updating and querying indexes.
 *
 * These are type specifiy default implementations. Theoretically a resource could implement it's own implementation.
 */
namespace Sink {
namespace ApplicationDomain {

template<>
class SINK_EXPORT TypeImplementation<Sink::ApplicationDomain::Mail> {
public:
    typedef Sink::ApplicationDomain::Buffer::Mail Buffer;
    typedef Sink::ApplicationDomain::Buffer::MailBuilder BufferBuilder;
    static void configure(TypeIndex &index);
    static void configure(PropertyMapper &propertyMapper);
    static void configure(IndexPropertyMapper &indexPropertyMapper);
    static QMap<QByteArray, int> typeDatabases();
};

template<>
class SINK_EXPORT TypeImplementation<Sink::ApplicationDomain::Folder> {
public:
    typedef Sink::ApplicationDomain::Buffer::Folder Buffer;
    typedef Sink::ApplicationDomain::Buffer::FolderBuilder BufferBuilder;
    static void configure(TypeIndex &);
    static void configure(PropertyMapper &);
    static void configure(IndexPropertyMapper &indexPropertyMapper);
    static QMap<QByteArray, int> typeDatabases();
};

template<>
class SINK_EXPORT TypeImplementation<Sink::ApplicationDomain::Contact> {
public:
    typedef Sink::ApplicationDomain::Buffer::Contact Buffer;
    typedef Sink::ApplicationDomain::Buffer::ContactBuilder BufferBuilder;
    static void configure(TypeIndex &);
    static void configure(PropertyMapper &);
    static void configure(IndexPropertyMapper &indexPropertyMapper);
    static QMap<QByteArray, int> typeDatabases();
};

template<>
class SINK_EXPORT TypeImplementation<Sink::ApplicationDomain::Addressbook> {
public:
    typedef Sink::ApplicationDomain::Buffer::Addressbook Buffer;
    typedef Sink::ApplicationDomain::Buffer::AddressbookBuilder BufferBuilder;
    static void configure(TypeIndex &);
    static void configure(PropertyMapper &);
    static void configure(IndexPropertyMapper &indexPropertyMapper);
    static QMap<QByteArray, int> typeDatabases();
};

template<>
class SINK_EXPORT TypeImplementation<Sink::ApplicationDomain::Event> {
public:
    typedef Sink::ApplicationDomain::Buffer::Event Buffer;
    typedef Sink::ApplicationDomain::Buffer::EventBuilder BufferBuilder;
    static void configure(TypeIndex &);
    static void configure(PropertyMapper &);
    static void configure(IndexPropertyMapper &indexPropertyMapper);
    static QMap<QByteArray, int> typeDatabases();
};

template<>
class SINK_EXPORT TypeImplementation<Sink::ApplicationDomain::Todo> {
public:
    typedef Sink::ApplicationDomain::Buffer::Todo Buffer;
    typedef Sink::ApplicationDomain::Buffer::TodoBuilder BufferBuilder;
    static void configure(TypeIndex &);
    static void configure(PropertyMapper &);
    static void configure(IndexPropertyMapper &indexPropertyMapper);
    static QMap<QByteArray, int> typeDatabases();
};

template<>
class SINK_EXPORT TypeImplementation<Sink::ApplicationDomain::Calendar> {
public:
    typedef Sink::ApplicationDomain::Buffer::Calendar Buffer;
    typedef Sink::ApplicationDomain::Buffer::CalendarBuilder BufferBuilder;
    static void configure(TypeIndex &);
    static void configure(PropertyMapper &);
    static void configure(IndexPropertyMapper &indexPropertyMapper);
    static QMap<QByteArray, int> typeDatabases();
};

}
}
