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

#include "../clientapi.h"

class ResultSet;
class QByteArray;

namespace Akonadi2 {
    class Query;

namespace ApplicationDomain {

/**
 * Implements all type-specific code such as updating and querying indexes.
 */
namespace EventImplementation {
    typedef Event DomainType;
    /**
     * Returns the potential result set based on the indexes.
     * 
     * An empty result set indicates that a full scan is required.
     */
    ResultSet queryIndexes(const Akonadi2::Query &query, const QByteArray &resourceInstanceIdentifier);
    void index(const Event &type);
};

}
}
