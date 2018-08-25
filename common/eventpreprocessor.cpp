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

#include "eventpreprocessor.h"

#include <KCalCore/ICalFormat>
#include <QDateTime>

void EventPropertyExtractor::updatedIndexedProperties(Event &event, const QByteArray &rawIcal)
{
    auto icalEvent = KCalCore::ICalFormat().readIncidence(rawIcal).dynamicCast<KCalCore::Event>();
    if(!icalEvent) {
        SinkWarning() << "Invalid ICal to process, ignoring: " << rawIcal;
        return;
    }
    SinkTrace() << "Extracting properties for event:" << icalEvent->summary();

    event.setExtractedUid(icalEvent->uid());
    event.setExtractedSummary(icalEvent->summary());
    event.setExtractedDescription(icalEvent->description());
    event.setExtractedStartTime(icalEvent->dtStart());
    event.setExtractedEndTime(icalEvent->dtEnd());
    event.setExtractedAllDay(icalEvent->allDay());
    event.setExtractedRecurring(icalEvent->recurs());

    QList<QPair<QDateTime, QDateTime>> periods;
    if (icalEvent->recurs() && icalEvent->recurrence()) {
        const auto duration = icalEvent->hasDuration() ? icalEvent->duration().asSeconds() : 0;
        const auto occurrences = icalEvent->recurrence()->timesInInterval(icalEvent->dtStart(), icalEvent->dtStart().addYears(10));
        for (const auto &start : occurrences) {
            periods.append(qMakePair(start, start.addSecs(duration)));
        }
    }
    event.setProperty("indexPeriods", QVariant::fromValue(periods));
}

void EventPropertyExtractor::newEntity(Event &event)
{
    updatedIndexedProperties(event, event.getIcal());
}

void EventPropertyExtractor::modifiedEntity(const Event &oldEvent, Event &newEvent)
{
    updatedIndexedProperties(newEvent, newEvent.getIcal());
}
