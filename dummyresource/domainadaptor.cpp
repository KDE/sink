
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

using namespace DummyCalendar;
using namespace flatbuffers;

//This will become a generic implementation that simply takes the resource buffer and local buffer pointer
class DummyEventAdaptor : public Akonadi2::Domain::BufferAdaptor
{
public:
    DummyEventAdaptor()
        : BufferAdaptor()
    {

    }

    void setProperty(const QByteArray &key, const QVariant &value)
    {
        if (mResourceMapper && mResourceMapper->mWriteAccessors.contains(key)) {
            // mResourceMapper->setProperty(key, value, mResourceBuffer);
        } else {
            // mLocalMapper.;
        }
    }

    virtual QVariant getProperty(const QByteArray &key) const
    {
        if (mResourceBuffer && mResourceMapper->mReadAccessors.contains(key)) {
            return mResourceMapper->getProperty(key, mResourceBuffer);
        } else if (mLocalBuffer && mLocalMapper->mReadAccessors.contains(key)) {
            return mLocalMapper->getProperty(key, mLocalBuffer);
        }
        qWarning() << "no mapping available for key " << key;
        return QVariant();
    }

    virtual QList<QByteArray> availableProperties() const
    {
        QList<QByteArray> props;
        props << mResourceMapper->mReadAccessors.keys();
        props << mLocalMapper->mReadAccessors.keys();
        return props;
    }

    Akonadi2::Domain::Buffer::Event const *mLocalBuffer;
    DummyEvent const *mResourceBuffer;

    QSharedPointer<PropertyMapper<Akonadi2::Domain::Buffer::Event> > mLocalMapper;
    QSharedPointer<PropertyMapper<DummyEvent> > mResourceMapper;
};


DummyEventAdaptorFactory::DummyEventAdaptorFactory()
    : DomainTypeAdaptorFactory()
{
    mResourceMapper = QSharedPointer<PropertyMapper<DummyEvent> >::create();
    mResourceMapper->mReadAccessors.insert("summary", [](DummyEvent const *buffer) -> QVariant {
        if (buffer->summary()) {
            return QString::fromStdString(buffer->summary()->c_str());
        }
        return QVariant();
    });
    mLocalMapper = QSharedPointer<PropertyMapper<Akonadi2::Domain::Buffer::Event> >::create();
    mLocalMapper->mReadAccessors.insert("summary", [](Akonadi2::Domain::Buffer::Event const *buffer) -> QVariant {
        if (buffer->summary()) {
            return QString::fromStdString(buffer->summary()->c_str());
        }
        return QVariant();
    });
    mLocalMapper->mReadAccessors.insert("uid", [](Akonadi2::Domain::Buffer::Event const *buffer) -> QVariant {
        if (buffer->uid()) {
            return QString::fromStdString(buffer->uid()->c_str());
        }
        return QVariant();
    });

}

//TODO pass EntityBuffer instead?
QSharedPointer<Akonadi2::Domain::BufferAdaptor> DummyEventAdaptorFactory::createAdaptor(const Akonadi2::Entity &entity)
{
    const auto resourceBuffer = Akonadi2::EntityBuffer::readBuffer<DummyEvent>(entity.resource());
    const auto localBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Domain::Buffer::Event>(entity.local());
    // const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(entity.metadata());

    auto adaptor = QSharedPointer<DummyEventAdaptor>::create();
    adaptor->mLocalBuffer = localBuffer;
    adaptor->mLocalMapper = mLocalMapper;
    adaptor->mResourceBuffer = resourceBuffer;
    adaptor->mResourceMapper = mResourceMapper;
    return adaptor;
}

void DummyEventAdaptorFactory::createBuffer(const Akonadi2::Domain::Event &event, flatbuffers::FlatBufferBuilder &fbb)
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
        auto localBuilder = Akonadi2::Domain::Buffer::EventBuilder(localFbb);
        localBuilder.add_uid(uid);
        auto location = localBuilder.Finish();
        Akonadi2::Domain::Buffer::FinishEventBuffer(localFbb, location);
    }

    Akonadi2::EntityBuffer::assembleEntityBuffer(fbb, 0, 0, eventFbb.GetBufferPointer(), eventFbb.GetSize(), localFbb.GetBufferPointer(), localFbb.GetSize());
}

