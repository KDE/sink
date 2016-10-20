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
#pragma once

#include "applicationdomaintype.h"

class QByteArray;

template<typename T>
class ReadPropertyMapper;
template<typename T>
class WritePropertyMapper;
class IndexPropertyMapper;

class TypeIndex;

namespace Sink {
namespace ApplicationDomain {
    namespace Buffer {
        struct Mail;
        struct MailBuilder;
    }

template<>
class TypeImplementation<Sink::ApplicationDomain::Mail> {
public:
    typedef Sink::ApplicationDomain::Buffer::Mail Buffer;
    typedef Sink::ApplicationDomain::Buffer::MailBuilder BufferBuilder;
    static void configure(TypeIndex &index);
    static void configure(ReadPropertyMapper<Buffer> &propertyMapper);
    static void configure(WritePropertyMapper<BufferBuilder> &propertyMapper);
    static void configure(IndexPropertyMapper &indexPropertyMapper);
};

}
}
