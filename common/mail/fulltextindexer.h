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

#include "indexer.h"

class FulltextIndex;
namespace Sink {

class FulltextIndexer : public Indexer
{
public:
    typedef QSharedPointer<FulltextIndexer> Ptr;
    virtual void add(const ApplicationDomain::ApplicationDomainType &entity) Q_DECL_OVERRIDE;
    virtual void remove(const ApplicationDomain::ApplicationDomainType &entity) Q_DECL_OVERRIDE;
    static QMap<QByteArray, int> databases();
private:
    QSharedPointer<FulltextIndex> index;
};

}
