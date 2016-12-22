/*
 * Copyright (C) 2016 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "query.h"

namespace Sink {
namespace StandardQueries {

    /**
     * Returns the complete thread, containing all mails from all folders.
     */
    static Query completeThread(const ApplicationDomain::Mail &mail)
    {
        Sink::Query query;
        query.setId("completethread");
        if (!mail.resourceInstanceIdentifier().isEmpty()) {
            query.resourceFilter(mail.resourceInstanceIdentifier());
        }
        query.filter(mail.identifier());
        query.sort<ApplicationDomain::Mail::Date>();
        query.bloom<ApplicationDomain::Mail::ThreadId>();
        return query;
    }

    /**
     * Returns thread leaders only, sorted by date.
     */
    static Query threadLeaders(const ApplicationDomain::Folder &folder)
    {
        Sink::Query query;
        query.setId("threadleaders");
        if (!folder.resourceInstanceIdentifier().isEmpty()) {
            query.resourceFilter(folder.resourceInstanceIdentifier());
        }
        query.filter<ApplicationDomain::Mail::Folder>(folder);
        query.sort<ApplicationDomain::Mail::Date>();
        query.reduce<ApplicationDomain::Mail::ThreadId>(Query::Reduce::Selector::max<ApplicationDomain::Mail::Date>())
            .count("count")
            .collect<ApplicationDomain::Mail::Unread>("unreadCollected")
            .collect<ApplicationDomain::Mail::Important>("importantCollected");
        return query;
    }

    /**
     * Outgoing mails.
     */
    static Query outboxMails()
    {
        Sink::Query query;
        query.setId("outbox");
        query.resourceContainsFilter<ApplicationDomain::SinkResource::Capabilities>(ApplicationDomain::ResourceCapabilities::Mail::transport);
        query.sort<ApplicationDomain::Mail::Date>();
        return query;
    }

}
}
