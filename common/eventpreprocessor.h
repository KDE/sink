/*
 *   Copyright (C) 2018 Christian Mollekopf <chrigi_1@fastmail.fm>
 *   Copyright (C) 2018 Rémi Nicole <minijackson@riseup.net>
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

#include "pipeline.h"
#include "sink_export.h"

class SINK_EXPORT EventPropertyExtractor : public Sink::EntityPreprocessor<Sink::ApplicationDomain::Event>
{
    using Event = Sink::ApplicationDomain::Event;

public:
    virtual ~EventPropertyExtractor() {}
    virtual void newEntity(Event &event) Q_DECL_OVERRIDE;
    virtual void modifiedEntity(const Event &oldEvent, Event &newEvent) Q_DECL_OVERRIDE;

private:
    static void updatedIndexedProperties(Event &event, const QByteArray &rawIcal);
};
