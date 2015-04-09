
#include "domainadaptor.h"

#include <QDebug>
#include <functional>

#include "dummycalendar_generated.h"
#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "domainadaptor.h"
#include <common/entitybuffer.h>

using namespace DummyCalendar;
using namespace flatbuffers;


DummyEventAdaptorFactory::DummyEventAdaptorFactory()
    : DomainTypeAdaptorFactory()
{
    //TODO turn this into initializePropertyMapper as well?
    mResourceMapper = QSharedPointer<PropertyMapper<DummyEvent> >::create();
    mResourceMapper->mReadAccessors.insert("summary", [](DummyEvent const *buffer) -> QVariant {
        if (buffer->summary()) {
            return QString::fromStdString(buffer->summary()->c_str());
        }
        return QVariant();
    });
}


void DummyEventAdaptorFactory::createBuffer(const Akonadi2::ApplicationDomain::Event &event, flatbuffers::FlatBufferBuilder &fbb)
{
    flatbuffers::FlatBufferBuilder eventFbb;
    eventFbb.Clear();
    {
        auto summary = eventFbb.CreateString(event.getProperty("summary").toString().toStdString());
        DummyCalendar::DummyEventBuilder eventBuilder(eventFbb);
        eventBuilder.add_summary(summary);
        auto eventLocation = eventBuilder.Finish();
        DummyCalendar::FinishDummyEventBuffer(eventFbb, eventLocation);
    }

    flatbuffers::FlatBufferBuilder localFbb;
    {
        auto uid = localFbb.CreateString(event.getProperty("uid").toString().toStdString());
        auto localBuilder = Akonadi2::ApplicationDomain::Buffer::EventBuilder(localFbb);
        localBuilder.add_uid(uid);
        auto location = localBuilder.Finish();
        Akonadi2::ApplicationDomain::Buffer::FinishEventBuffer(localFbb, location);
    }

    Akonadi2::EntityBuffer::assembleEntityBuffer(fbb, 0, 0, eventFbb.GetBufferPointer(), eventFbb.GetSize(), localFbb.GetBufferPointer(), localFbb.GetSize());
}

