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

#include "todopreprocessor.h"

#include <KCalCore/ICalFormat>

static QString statusString(const KCalCore::Todo &incidence)
{
    switch(incidence.status()) {
        case KCalCore::Incidence::StatusCompleted:
            return "COMPLETED";
        case KCalCore::Incidence::StatusNeedsAction:
            return "NEEDSACTION";
        case KCalCore::Incidence::StatusCanceled:
            return "CANCELED";
        case KCalCore::Incidence::StatusInProcess:
            return "INPROCESS";
        default:
            break;
    }
    return incidence.customStatus();
}

void TodoPropertyExtractor::updatedIndexedProperties(Todo &todo, const QByteArray &rawIcal)
{
    auto incidence = KCalCore::ICalFormat().readIncidence(rawIcal);

    if(!incidence) {
        SinkWarning() << "Invalid ICal to process, ignoring...";
        return;
    }

    if(incidence->type() != KCalCore::IncidenceBase::IncidenceType::TypeTodo) {
        SinkWarning() << "ICal to process is not of type `Todo`, ignoring...";
        return;
    }

    auto icalTodo = dynamic_cast<const KCalCore::Todo *>(incidence.data());
    // Should be guaranteed by the incidence->type() condition above.
    Q_ASSERT(icalTodo);

    SinkTrace() << "Extracting properties for todo:" << icalTodo->summary();

    todo.setExtractedUid(icalTodo->uid());
    todo.setExtractedSummary(icalTodo->summary());
    todo.setExtractedDescription(icalTodo->description());

    // Sets invalid QDateTime if not defined
    todo.setExtractedCompletedDate(icalTodo->completed());
    todo.setExtractedDueDate(icalTodo->dtDue());
    todo.setExtractedStartDate(icalTodo->dtStart());

    todo.setExtractedStatus(statusString(*icalTodo));
    todo.setExtractedPriority(icalTodo->priority());
    todo.setExtractedCategories(icalTodo->categories());
}

void TodoPropertyExtractor::newEntity(Todo &todo)
{
    updatedIndexedProperties(todo, todo.getIcal());
}

void TodoPropertyExtractor::modifiedEntity(const Todo &oldTodo, Todo &newTodo)
{
    updatedIndexedProperties(newTodo, newTodo.getIcal());
}
