
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

/**
 * Defines how to convert qt primitives to flatbuffer ones
 * TODO: rename to createProperty or so?
 */
template <class T>
uoffset_t extractProperty(const QVariant &, flatbuffers::FlatBufferBuilder &fbb);

template <>
uoffset_t extractProperty<QString>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        return fbb.CreateString(property.toString().toStdString()).o;
    }
    return 0;
}

/**
 * Create a buffer from a domain object using the provided mappings
 */
template <class Builder>
void createBufferPart(const Akonadi2::ApplicationDomain::ApplicationDomainType &domainObject, flatbuffers::FlatBufferBuilder &fbb, const WritePropertyMapper<Builder> &mapper)
{
    //First create a primitives such as strings using the mappings
    QList<std::function<void(Builder &)> > propertiesToAddToResource;
    for (const auto &property : domainObject.changedProperties()) {
        const auto value = domainObject.getProperty(property);
        if (mapper.hasMapping(property)) {
            mapper.setProperty(property, domainObject.getProperty(property), propertiesToAddToResource, fbb);
        }
    }

    //Then create all porperties using the above generated builderCalls
    Builder builder(fbb);
    for (auto propertyBuilder : propertiesToAddToResource) {
        propertyBuilder(builder);
    }
    builder.Finish();
}



DummyEventAdaptorFactory::DummyEventAdaptorFactory()
    : DomainTypeAdaptorFactory()
{
    //TODO turn this into initializeReadPropertyMapper as well?
    mResourceMapper = QSharedPointer<ReadPropertyMapper<DummyEvent> >::create();
    mResourceMapper->addMapping("summary", [](DummyEvent const *buffer) -> QVariant {
        if (buffer->summary()) {
            return QString::fromStdString(buffer->summary()->c_str());
        }
        return QVariant();
    });

    mResourceWriteMapper->addMapping("summary", [](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(DummyEventBuilder &)> {
        auto offset = extractProperty<QString>(value, fbb);
        return [offset](DummyEventBuilder &builder) { builder.add_summary(offset); };
    });
}


void DummyEventAdaptorFactory::createBuffer(const Akonadi2::ApplicationDomain::Event &event, flatbuffers::FlatBufferBuilder &fbb)
{
    // flatbuffers::FlatBufferBuilder resFbb;
    // flatbuffers::FlatBufferBuilder localFbb;

    // QList<std::function<void(DummyEventBuilder &)> > propertiesToAddToResource;
    // QList<std::function<void(Akonadi2::ApplicationDomain::Buffer::EventBuilder &)> > propertiesToAddToLocal;
    // for (const auto &property : event.changedProperties()) {
    //     const auto value = event.getProperty(property);
    //     if (mResourceWriteMapper && mResourceWriteMapper->hasMapping(property)) {
    //         mResourceWriteMapper->setProperty(property, value, propertiesToAddToResource, resFbb);
    //     } if (mLocalWriteMapper && mLocalWriteMapper->hasMapping(property)) {
    //         mLocalWriteMapper->setProperty(property, value, propertiesToAddToLocal, localFbb);
    //     }
    // }

    // DummyEventBuilder resBuilder(resFbb);
    // for (auto propertyBuilder : propertiesToAddToResource) {
    //     propertyBuilder(resBuilder);
    // }
    // resBuilder.Finish();

    // DummyEventBuilder localBuilder(localFbb);
    // for (auto propertyBuilder : propertiesToAddToResource) {
    //     propertyBuilder(localBuilder);
    // }
    // localBuilder.Finish();

    // TODO: how does a resource specify what goes to a local buffer and what it stores separately?
    // flatbuffers::FlatBufferBuilder eventFbb;
    // {
    //     auto summary = extractProperty<QString>(event.getProperty("summary"), fbb);
    //     DummyCalendar::DummyEventBuilder eventBuilder(eventFbb);
    //     eventBuilder.add_summary(summary);
    //     auto eventLocation = eventBuilder.Finish();
    //     DummyCalendar::FinishDummyEventBuffer(eventFbb, eventLocation);
    // }

    //TODO we should only copy values into the local buffer that haven't already been copied by the resource buffer
    flatbuffers::FlatBufferBuilder localFbb;
    if (mLocalWriteMapper) {
        createBufferPart<Akonadi2::ApplicationDomain::Buffer::EventBuilder>(event, localFbb, *mLocalWriteMapper);
    }

    flatbuffers::FlatBufferBuilder resFbb;
    if (mResourceWriteMapper) {
        createBufferPart<DummyEventBuilder>(event, resFbb, *mResourceWriteMapper);
    }

    Akonadi2::EntityBuffer::assembleEntityBuffer(fbb, 0, 0, resFbb.GetBufferPointer(), resFbb.GetSize(), localFbb.GetBufferPointer(), localFbb.GetSize());
}

