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
#include "todopreprocessor.h"
#include "facade.h"
#include "facadefactory.h"

#include <QColor>

#define ENTITY_TYPE_EVENT "event"
#define ENTITY_TYPE_TODO "todo"
#define ENTITY_TYPE_CALENDAR "calendar"

using Sink::ApplicationDomain::getTypeName;
using namespace Sink;

class CalDAVSynchronizer : public WebDavSynchronizer
{
    using Event = Sink::ApplicationDomain::Event;
    using Todo = Sink::ApplicationDomain::Todo;
    using Calendar = Sink::ApplicationDomain::Calendar;

public:
    explicit CalDAVSynchronizer(const Sink::ResourceContext &context)
        : WebDavSynchronizer(context, KDAV2::CalDav, getTypeName<Calendar>(), {getTypeName<Event>(), getTypeName<Todo>()})
    {
    }

protected:
    void updateLocalCollections(KDAV2::DavCollection::List calendarList) Q_DECL_OVERRIDE
    {
        SinkLog() << "Found" << calendarList.size() << "calendar(s)";

        QVector<QByteArray> ridList;
        for (const auto &remoteCalendar : calendarList) {
            const auto &rid = resourceID(remoteCalendar);

            Calendar localCalendar;
            localCalendar.setName(remoteCalendar.displayName());
            localCalendar.setColor(remoteCalendar.color().name().toLatin1());

            if (remoteCalendar.contentTypes() & KDAV2::DavCollection::Events) {
                localCalendar.setContentTypes({"event"});
            }
            if (remoteCalendar.contentTypes() & KDAV2::DavCollection::Todos) {
                localCalendar.setContentTypes({"todo"});
            }
            if (remoteCalendar.contentTypes() & KDAV2::DavCollection::Calendar) {
                localCalendar.setContentTypes({"event", "todo"});
            }

            const auto sinkId = syncStore().resolveRemoteId(ENTITY_TYPE_CALENDAR, rid);
            const auto found = store().contains(ENTITY_TYPE_CALENDAR, sinkId);
            SinkLog() << "Found calendar:" << remoteCalendar.displayName() << "[" << rid << "]" << remoteCalendar.contentTypes() << (found ? " (existing)" : "");

            //Set default when creating, otherwise don't touch
            if (!found) {
                localCalendar.setEnabled(false);
            }

            createOrModify(ENTITY_TYPE_CALENDAR, rid, localCalendar);
        }
    }

    void updateLocalItem(const KDAV2::DavItem &remoteItem, const QByteArray &calendarLocalId) Q_DECL_OVERRIDE
    {
        const auto rid = resourceID(remoteItem);

        const auto ical = remoteItem.data();

        if (ical.contains("BEGIN:VEVENT")) {
            Event localEvent;
            localEvent.setIcal(ical);
            localEvent.setCalendar(calendarLocalId);

            SinkTrace() << "Found an event with id:" << rid;

            createOrModify(ENTITY_TYPE_EVENT, rid, localEvent, {});
        } else if (ical.contains("BEGIN:VTODO")) {
            Todo localTodo;
            localTodo.setIcal(ical);
            localTodo.setCalendar(calendarLocalId);

            SinkTrace() << "Found a Todo with id:" << rid;

            createOrModify(ENTITY_TYPE_TODO, rid, localTodo, {});
        } else {
                SinkWarning() << "Trying to add a 'Unknown' item";
        }
    }

    template<typename Item>
    KAsync::Job<QByteArray> replayItem(const Item &localItem, Sink::Operation operation,
        const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties,
        const QByteArray &entityType)
    {
        SinkLog() << "Replaying" << entityType;

        KDAV2::DavItem remoteItem;

        switch (operation) {
            case Sink::Operation_Creation: {
                auto rawIcal = localItem.getIcal();
                if (rawIcal.isEmpty()) {
                    return KAsync::error<QByteArray>("No ICal in item for creation replay");
                }
                return createItem(rawIcal, "text/calendar", localItem.getUid().toUtf8() + ".ics", syncStore().resolveLocalId(ENTITY_TYPE_CALENDAR, localItem.getCalendar()));
            }
            case Sink::Operation_Removal: {
                return removeItem(oldRemoteId);
            }
            case Sink::Operation_Modification:
                auto rawIcal = localItem.getIcal();
                if (rawIcal.isEmpty()) {
                    return KAsync::error<QByteArray>("No ICal in item for modification replay");
                }

                //Not pretty but all ical types happen to have a calendar property of the same name.
                if (changedProperties.contains(ApplicationDomain::Event::Calendar::name)) {
                    return moveItem(rawIcal, "text/calendar", localItem.getUid().toUtf8() + ".ics", syncStore().resolveLocalId(ENTITY_TYPE_CALENDAR, localItem.getCalendar()), oldRemoteId);
                }

                return modifyItem(oldRemoteId, rawIcal, "text/calendar", syncStore().resolveLocalId(ENTITY_TYPE_CALENDAR, localItem.getCalendar()));
        }
        return KAsync::null<QByteArray>();
    }

    KAsync::Job<QByteArray> replay(const Event &event, Sink::Operation operation,
        const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        return replayItem(event, operation, oldRemoteId, changedProperties, ENTITY_TYPE_EVENT);
    }

    KAsync::Job<QByteArray> replay(const Todo &todo, Sink::Operation operation,
        const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        return replayItem(todo, operation, oldRemoteId, changedProperties, ENTITY_TYPE_TODO);
    }

    KAsync::Job<QByteArray> replay(const Calendar &calendar, Sink::Operation operation,
        const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        SinkLog() << "Replaying calendar" << changedProperties;

        switch (operation) {
            case Sink::Operation_Creation: {
                SinkLog() << "Replaying calendar creation";
                KDAV2::DavCollection collection;

                collection.setDisplayName(calendar.getName());
                if (calendar.getContentTypes().contains("event")) {
                    collection.setContentTypes(KDAV2::DavCollection::Events);
                }
                if (calendar.getContentTypes().contains("todo")) {
                    collection.setContentTypes(KDAV2::DavCollection::Todos);
                }

                return createCollection(collection);
            }
            case Sink::Operation_Removal:
                SinkLog() << "Replaying calendar removal";
                return removeCollection(oldRemoteId);
            case Sink::Operation_Modification: {
                SinkLog() << "Replaying calendar modification";
                if (calendar.getEnabled() && changedProperties.contains(Calendar::Enabled::name)) {
                    //Trigger synchronization of that calendar
                    Query scope;
                    scope.setType<Event>();
                    scope.filter<Event::Calendar>(calendar);
                    synchronize(scope);
                    if (changedProperties.size() == 1) {
                        return KAsync::value(oldRemoteId);
                    }
                }
                KDAV2::DavCollection collection;
                collection.setDisplayName(calendar.getName());
                collection.setColor(QColor{QString{calendar.getColor()}});
                if (calendar.getContentTypes().contains("event")) {
                    collection.setContentTypes(KDAV2::DavCollection::Events);
                }
                if (calendar.getContentTypes().contains("todo")) {
                    collection.setContentTypes(KDAV2::DavCollection::Todos);
                }
                return modifyCollection(oldRemoteId, collection);
            }
        }

        return KAsync::null<QByteArray>();
    }
};

class CollectionCleanupPreprocessor : public Sink::Preprocessor
{
public:
    virtual void deletedEntity(const ApplicationDomain::ApplicationDomainType &oldEntity) Q_DECL_OVERRIDE
    {
        //Remove all events of a collection when removing the collection.
        const auto revision = entityStore().maxRevision();
        entityStore().indexLookup<ApplicationDomain::Event, ApplicationDomain::Event::Calendar>(oldEntity.identifier(), [&] (const QByteArray &identifier) {
            deleteEntity(ApplicationDomain::ApplicationDomainType{{}, identifier, revision, {}}, ApplicationDomain::getTypeName<ApplicationDomain::Event>(), false);
        });
        entityStore().indexLookup<ApplicationDomain::Todo, ApplicationDomain::Event::Calendar>(oldEntity.identifier(), [&] (const QByteArray &identifier) {
            deleteEntity(ApplicationDomain::ApplicationDomainType{{}, identifier, revision, {}}, ApplicationDomain::getTypeName<ApplicationDomain::Todo>(), false);
        });
    }
};

CalDavResource::CalDavResource(const Sink::ResourceContext &context)
    : Sink::GenericResource(context)
{
    auto synchronizer = QSharedPointer<CalDAVSynchronizer>::create(context);
    setupSynchronizer(synchronizer);

    setupPreprocessors(ENTITY_TYPE_EVENT, {new EventPropertyExtractor});
    setupPreprocessors(ENTITY_TYPE_TODO, {new TodoPropertyExtractor});
    setupPreprocessors(ENTITY_TYPE_CALENDAR, {new CollectionCleanupPreprocessor});
}

CalDavResourceFactory::CalDavResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent, {
                                        Sink::ApplicationDomain::ResourceCapabilities::Event::calendar,
                                        Sink::ApplicationDomain::ResourceCapabilities::Event::event,
                                        Sink::ApplicationDomain::ResourceCapabilities::Event::storage,
                                        Sink::ApplicationDomain::ResourceCapabilities::Todo::todo,
                                        Sink::ApplicationDomain::ResourceCapabilities::Todo::storage,
                                    })
{
}

Sink::Resource *CalDavResourceFactory::createResource(const Sink::ResourceContext &context)
{
    return new CalDavResource(context);
}

using Sink::ApplicationDomain::Calendar;
using Sink::ApplicationDomain::Event;
using Sink::ApplicationDomain::Todo;

void CalDavResourceFactory::registerFacades(const QByteArray &resourceName, Sink::FacadeFactory &factory)
{
    factory.registerFacade<Event, Sink::DefaultFacade<Event>>(resourceName);
    factory.registerFacade<Todo, Sink::DefaultFacade<Todo>>(resourceName);
    factory.registerFacade<Calendar, Sink::DefaultFacade<Calendar>>(resourceName);
}


void CalDavResourceFactory::registerAdaptorFactories(
    const QByteArray &resourceName, Sink::AdaptorFactoryRegistry &registry)
{
    registry.registerFactory<Event, DefaultAdaptorFactory<Event>>(resourceName);
    registry.registerFactory<Todo, DefaultAdaptorFactory<Todo>>(resourceName);
    registry.registerFactory<Calendar, DefaultAdaptorFactory<Calendar>>(resourceName);
}

void CalDavResourceFactory::removeDataFromDisk(const QByteArray &instanceIdentifier)
{
    CalDavResource::removeFromDisk(instanceIdentifier);
}
