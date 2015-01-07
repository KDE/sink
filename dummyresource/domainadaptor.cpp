
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

    void setProperty(const QString &key, const QVariant &value)
    {
        if (mResourceMapper->mWriteAccessors.contains(key)) {
            // mResourceMapper.setProperty(key, value, mResourceBuffer);
        } else {
            // mLocalMapper.;
        }
    }

    virtual QVariant getProperty(const QString &key) const
    {
        if (mResourceBuffer && mResourceMapper->mReadAccessors.contains(key)) {
            return mResourceMapper->getProperty(key, mResourceBuffer);
        } else if (mLocalBuffer) {
            return mLocalMapper->getProperty(key, mLocalBuffer);
        }
        return QVariant();
    }

    virtual QStringList availableProperties() const
    {
        QStringList props;
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
        return QString::fromStdString(buffer->summary()->c_str());
    });
    //TODO set accessors for all properties

}

//TODO pass EntityBuffer instead?
QSharedPointer<Akonadi2::Domain::BufferAdaptor> DummyEventAdaptorFactory::createAdaptor(const Akonadi2::Entity &entity)
{
    DummyEvent const *resourceBuffer = 0;
    if (auto resourceData = entity.resource()) {
        flatbuffers::Verifier verifyer(resourceData->Data(), resourceData->size());
        if (VerifyDummyEventBuffer(verifyer)) {
            resourceBuffer = GetDummyEvent(resourceData->Data());
        }
    }

    Akonadi2::Metadata const *metadataBuffer = 0;
    if (auto metadataData = entity.metadata()) {
        flatbuffers::Verifier verifyer(metadataData->Data(), metadataData->size());
        if (Akonadi2::VerifyMetadataBuffer(verifyer)) {
            metadataBuffer = Akonadi2::GetMetadata(metadataData);
        }
    }

    Akonadi2::Domain::Buffer::Event const *localBuffer = 0;
    if (auto localData = entity.local()) {
        flatbuffers::Verifier verifyer(localData->Data(), localData->size());
        if (Akonadi2::Domain::Buffer::VerifyEventBuffer(verifyer)) {
            localBuffer = Akonadi2::Domain::Buffer::GetEvent(localData);
        }
    }

    auto adaptor = QSharedPointer<DummyEventAdaptor>::create();
    adaptor->mLocalBuffer = localBuffer;
    adaptor->mResourceBuffer = resourceBuffer;
    adaptor->mResourceMapper = mResourceMapper;
    adaptor->mLocalMapper = mLocalMapper;
    return adaptor;
}

