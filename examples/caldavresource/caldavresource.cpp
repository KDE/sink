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
#include "eventpreprocessor.h"
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

    KAsync::Job<QByteArray> replay(const Event &event, Sink::Operation operation,
        const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        SinkLog() << "Replaying event";

        KDAV2::DavItem item;

        switch (operation) {
            case Sink::Operation_Creation: {
                auto rawIcal = event.getIcal();
                if(rawIcal == "") {
                    return KAsync::error<QByteArray>("No ICal in event for creation replay");
                }

                auto collectionId = syncStore().resolveLocalId(ENTITY_TYPE_CALENDAR, event.getCalendar());

                item.setData(rawIcal);
                item.setContentType("text/calendar");
                item.setUrl(urlOf(collectionId, event.getUid()));

                SinkLog() << "Creating event:" << event.getSummary();
                return createItem(item).then([item] { return resourceID(item); });
            }
            case Sink::Operation_Removal: {
                // We only need the URL in the DAV item for removal
                item.setUrl(urlOf(oldRemoteId));

                SinkLog() << "Removing event:" << oldRemoteId;
                return removeItem(item).then([] { return QByteArray{}; });
            }
            case Sink::Operation_Modification:
                auto rawIcal = event.getIcal();
                if(rawIcal == "") {
                    return KAsync::error<QByteArray>("No ICal in event for modification replay");
                }

                item.setData(rawIcal);
                item.setContentType("text/calendar");
                item.setUrl(urlOf(oldRemoteId));

                SinkLog() << "Modifying event:" << event.getSummary();

                // It would be nice to check that the URL of the item hasn't
                // changed and move he item if it did, but since the URL is
                // pretty much arbitrary, whoe does that anyway?
                return modifyItem(item).then([oldRemoteId] { return oldRemoteId; });
        }
    }

    KAsync::Job<QByteArray> replay(const Calendar &calendar, Sink::Operation operation,
        const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        switch (operation) {
            case Sink::Operation_Creation:
                SinkWarning() << "Unimplemented replay of calendar creation";
                break;
            case Sink::Operation_Removal:
                SinkLog() << "Replaying calendar removal";
                removeCollection(urlOf(oldRemoteId));
                break;
            case Sink::Operation_Modification:
                SinkWarning() << "Unimplemented replay of calendar modification";
                break;
        }

        return KAsync::null<QByteArray>();
    }
};

CalDavResource::CalDavResource(const Sink::ResourceContext &context)
    : Sink::GenericResource(context)
{
    auto synchronizer = QSharedPointer<EventSynchronizer>::create(context);
    setupSynchronizer(synchronizer);

    setupPreprocessors(ENTITY_TYPE_EVENT, QVector<Sink::Preprocessor*>() << new EventPropertyExtractor);
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
