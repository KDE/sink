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
        index.reset(new FulltextIndex{mResourceInstanceIdentifier, Storage::DataStore::ReadWrite});
    }
    index->add(Sink::Storage::Identifier::fromDisplayByteArray(entity.identifier()), entity.getProperty("index").value<QList<QPair<QString, QString>>>());
}

void FulltextIndexer::remove(const ApplicationDomain::ApplicationDomainType &entity)
{
    if (!index) {
        index.reset(new FulltextIndex{mResourceInstanceIdentifier, Storage::DataStore::ReadWrite});
    }
    index->remove(Sink::Storage::Identifier::fromDisplayByteArray(entity.identifier()));
}

void FulltextIndexer::commitTransaction()
{
    if (index) {
        index->commitTransaction();
    }
}

void FulltextIndexer::abortTransaction()
{
    if (index) {
        index->abortTransaction();
    }
}

QMap<QByteArray, int> FulltextIndexer::databases()
{
    return {};
}

