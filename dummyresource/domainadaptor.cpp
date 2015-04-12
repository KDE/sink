
#include "domainadaptor.h"

#include <QDebug>
#include <functional>

#include "dummycalendar_generated.h"
#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "domainadaptor.h"
#include "log.h"
#include <common/entitybuffer.h>

using namespace DummyCalendar;
using namespace flatbuffers;




DummyEventAdaptorFactory::DummyEventAdaptorFactory()
    : DomainTypeAdaptorFactory()
{
    //TODO turn this into initializeReadPropertyMapper as well?
    mResourceMapper->addMapping("summary", [](DummyEvent const *buffer) -> QVariant {
        return propertyToVariant<QString>(buffer->summary());
    });

    mResourceWriteMapper->addMapping("summary", [](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(DummyEventBuilder &)> {
        auto offset = variantToProperty<QString>(value, fbb);
        return [offset](DummyEventBuilder &builder) { builder.add_summary(offset); };
    });
}


void DummyEventAdaptorFactory::createBuffer(const Akonadi2::ApplicationDomain::Event &event, flatbuffers::FlatBufferBuilder &fbb)
{
    flatbuffers::FlatBufferBuilder localFbb;
    if (mLocalWriteMapper) {
        auto pos = createBufferPart<Akonadi2::ApplicationDomain::Buffer::EventBuilder, Akonadi2::ApplicationDomain::Buffer::Event>(event, localFbb, *mLocalWriteMapper);
        Akonadi2::ApplicationDomain::Buffer::FinishEventBuffer(localFbb, pos);
        flatbuffers::Verifier verifier(localFbb.GetBufferPointer(), localFbb.GetSize());
        if (!verifier.VerifyBuffer<Akonadi2::ApplicationDomain::Buffer::Event>()) {
            Warning() << "Created invalid local buffer";
        }
    }

    flatbuffers::FlatBufferBuilder resFbb;
    if (mResourceWriteMapper) {
        auto pos = createBufferPart<DummyEventBuilder, DummyEvent>(event, resFbb, *mResourceWriteMapper);
        DummyCalendar::FinishDummyEventBuffer(resFbb, pos);
        flatbuffers::Verifier verifier(resFbb.GetBufferPointer(), resFbb.GetSize());
        if (!verifier.VerifyBuffer<DummyEvent>()) {
            Warning() << "Created invalid resource buffer";
        }
    }

    Akonadi2::EntityBuffer::assembleEntityBuffer(fbb, 0, 0, resFbb.GetBufferPointer(), resFbb.GetSize(), localFbb.GetBufferPointer(), localFbb.GetSize());
}

