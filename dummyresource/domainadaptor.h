
#pragma once

#include "common/domainadaptor.h"
#include "event_generated.h"
#include "dummycalendar_generated.h"
#include "entity_generated.h"

class DummyEventAdaptorFactory : public DomainTypeAdaptorFactory<Akonadi2::Domain::Event, Akonadi2::Domain::Buffer::Event, DummyCalendar::DummyEvent>
{
public:
    DummyEventAdaptorFactory();
    virtual QSharedPointer<Akonadi2::Domain::BufferAdaptor> createAdaptor(const Akonadi2::Entity &entity);
    virtual void createBuffer(const Akonadi2::Domain::Event &event, flatbuffers::FlatBufferBuilder &fbb);
};
