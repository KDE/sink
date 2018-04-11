/*
 *   Copyright (C) 2018 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "caldavresource.h"

#include "../webdavcommon/webdav.h"

#include "adaptorfactoryregistry.h"
#include "applicationdomaintype.h"
#include "domainadaptor.h"
#include "facade.h"
#include "facadefactory.h"

#include <KCalCore/ICalFormat>

#define ENTITY_TYPE_EVENT "event"
#define ENTITY_TYPE_CALENDAR "calendar"

using Sink::ApplicationDomain::getTypeName;

class EventSynchronizer : public WebDavSynchronizer
{
    using Event = Sink::ApplicationDomain::Event;
    using Calendar = Sink::ApplicationDomain::Calendar;

public:
    explicit EventSynchronizer(const Sink::ResourceContext &context)
        : WebDavSynchronizer(context, KDAV2::CalDav, getTypeName<Calendar>(), getTypeName<Event>())
    {}

protected:
    void updateLocalCollections(KDAV2::DavCollection::List calendarList) Q_DECL_OVERRIDE
    {
        SinkLog() << "Found" << calendarList.size() << "calendar(s)";

        QVector<QByteArray> ridList;
        for (const auto &remoteCalendar : calendarList) {
            const auto &rid = resourceID(remoteCalendar);
            SinkLog() << "Found calendar:" << remoteCalendar.displayName() << "[" << rid << "]";

            Calendar localCalendar;
            localCalendar.setName(remoteCalendar.displayName());

            createOrModify(ENTITY_TYPE_CALENDAR, rid, localCalendar,
                /* mergeCriteria = */ QHash<QByteArray, Sink::Query::Comparator>{});
        }
    }

    void updateLocalItem(KDAV2::DavItem remoteItem, const QByteArray &calendarLocalId) Q_DECL_OVERRIDE
    {
        const auto &rid = resourceID(remoteItem);

        auto ical = remoteItem.data();
        auto incidence = KCalCore::ICalFormat().fromString(ical);

        using Type = KCalCore::IncidenceBase::IncidenceType;

        switch (incidence->type()) {
            case Type::TypeEvent: {
                auto remoteEvent = dynamic_cast<const KCalCore::Event &>(*incidence);

                Event localEvent;
                localEvent.setUid(remoteEvent.uid());
                localEvent.setSummary(remoteEvent.summary());
                localEvent.setDescription(remoteEvent.description());
                localEvent.setStartTime(remoteEvent.dtStart());
                localEvent.setEndTime(remoteEvent.dtEnd());
                localEvent.setIcal(ical);
                localEvent.setCalendar(calendarLocalId);

                SinkTrace() << "Found an event:" << localEvent.getSummary() << "with id:" << rid;

                createOrModify(ENTITY_TYPE_EVENT, rid, localEvent,
                    /* mergeCriteria = */ QHash<QByteArray, Sink::Query::Comparator>{});
                break;
            }
            case Type::TypeTodo:
                SinkWarning() << "Unimplemented add of a 'Todo' item in the Store";
                break;
            case Type::TypeJournal:
                SinkWarning() << "Unimplemented add of a 'Journal' item in the Store";
                break;
            case Type::TypeFreeBusy:
                SinkWarning() << "Unimplemented add of a 'FreeBusy' item in the Store";
                break;
            case Type::TypeUnknown:
                SinkWarning() << "Trying to add a 'Unknown' item";
                break;
            default:
                break;
        }
    }

    QByteArray collectionLocalResourceID(const KDAV2::DavCollection &calendar) Q_DECL_OVERRIDE
    {
        return syncStore().resolveRemoteId(ENTITY_TYPE_CALENDAR, resourceID(calendar));
    }
};

CalDavResource::CalDavResource(const Sink::ResourceContext &context)
    : Sink::GenericResource(context)
{
    auto synchronizer = QSharedPointer<EventSynchronizer>::create(context);
    setupSynchronizer(synchronizer);

    // setupPreprocessors(ENTITY_TYPE_EVENT, QVector<Sink::Preprocessor*>() << new EventPropertyExtractor);
}

CalDavResourceFactory::CalDavResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent, {
                                        Sink::ApplicationDomain::ResourceCapabilities::Event::event,
                                        Sink::ApplicationDomain::ResourceCapabilities::Event::calendar,
                                        Sink::ApplicationDomain::ResourceCapabilities::Event::storage,
                                    })
{}

Sink::Resource *CalDavResourceFactory::createResource(const Sink::ResourceContext &context)
{
    return new CalDavResource(context);
}

using Sink::ApplicationDomain::Calendar;
using Sink::ApplicationDomain::Event;

void CalDavResourceFactory::registerFacades(const QByteArray &resourceName, Sink::FacadeFactory &factory)
{
    factory.registerFacade<Event, Sink::DefaultFacade<Event>>(resourceName);
    factory.registerFacade<Calendar, Sink::DefaultFacade<Calendar>>(resourceName);
}


void CalDavResourceFactory::registerAdaptorFactories(
    const QByteArray &resourceName, Sink::AdaptorFactoryRegistry &registry)
{
    registry.registerFactory<Event, DefaultAdaptorFactory<Event>>(resourceName);
    registry.registerFactory<Calendar, DefaultAdaptorFactory<Calendar>>(resourceName);
}

void CalDavResourceFactory::removeDataFromDisk(const QByteArray &instanceIdentifier)
{
    CalDavResource::removeFromDisk(instanceIdentifier);
}
