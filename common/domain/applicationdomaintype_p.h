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

#include "applicationdomaintype.h"

template <template<typename> class Func>
struct TypeHelper {
    const QByteArray type;
    TypeHelper(const QByteArray &type_)
        : type(type_)
    {

    }

    template <typename R, typename ...Args>
    R operator()(Args && ... args) const {
        if (type == Sink::ApplicationDomain::getTypeName<Sink::ApplicationDomain::Folder>()) {
            return Func<Sink::ApplicationDomain::Folder>{}(std::forward<Args...>(args...)); 
        } else if (type == Sink::ApplicationDomain::getTypeName<Sink::ApplicationDomain::Mail>()) {
            return Func<Sink::ApplicationDomain::Mail>{}(std::forward<Args...>(args...)); 
        } else if (type == Sink::ApplicationDomain::getTypeName<Sink::ApplicationDomain::Event>()) {
            return Func<Sink::ApplicationDomain::Event>{}(std::forward<Args...>(args...)); 
        } else if (type == Sink::ApplicationDomain::getTypeName<Sink::ApplicationDomain::Contact>()) {
            return Func<Sink::ApplicationDomain::Contact>{}(std::forward<Args...>(args...));
        } else {
            Q_ASSERT(false);
        }
    }
};
