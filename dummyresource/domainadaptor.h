#pragma once

#include "common/domainadaptor.h"
#include "event_generated.h"
#include "dummycalendar_generated.h"
#include "entity_generated.h"

class DummyEventAdaptorFactory : public DomainTypeAdaptorFactory<Akonadi2::ApplicationDomain::Event, Akonadi2::ApplicationDomain::Buffer::Event, DummyCalendar::DummyEvent, Akonadi2::ApplicationDomain::Buffer::EventBuilder, DummyCalendar::DummyEventBuilder>
{
public:
    DummyEventAdaptorFactory();
    virtual ~DummyEventAdaptorFactory() {};
    virtual void createBuffer(const Akonadi2::ApplicationDomain::Event &event, flatbuffers::FlatBufferBuilder &fbb);
};
