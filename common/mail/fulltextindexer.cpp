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
#include "fulltextindexer.h"

#include "typeindex.h"
#include "fulltextindex.h"
#include "log.h"
#include "utils.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;


void FulltextIndexer::add(const ApplicationDomain::ApplicationDomainType &entity)
{
    if (!index) {
        //FIXME
        index.reset(new FulltextIndex{"sink.dummy.instance1", "subject", Storage::DataStore::ReadWrite});
    }
    index->add(entity.identifier(), entity.getProperty(ApplicationDomain::Mail::Subject::name).toString());
}

void FulltextIndexer::modify(const ApplicationDomain::ApplicationDomainType &old, const ApplicationDomain::ApplicationDomainType &entity)
{

}

void FulltextIndexer::remove(const ApplicationDomain::ApplicationDomainType &entity)
{
    // auto messageId = entity.getProperty(Mail::MessageId::name);
    // auto thread = index().secondaryLookup<Mail::MessageId, Mail::ThreadId>(messageId);
    // index().unindex<Mail::MessageId, Mail::ThreadId>(messageId.toByteArray(), thread.first(), transaction());
    // index().unindex<Mail::ThreadId, Mail::MessageId>(thread.first(), messageId.toByteArray(), transaction());
}

QMap<QByteArray, int> FulltextIndexer::databases()
{
    return {};
}

