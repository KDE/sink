#include "entitybuffer.h"

#include "entity_generated.h"
#include "metadata_generated.h"
#include <QDebug>

using namespace Akonadi2;

EntityBuffer::EntityBuffer(const void *dataValue, int dataSize)
    : mEntity(nullptr)
{
    flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(dataValue), dataSize);
    // Q_ASSERT(Akonadi2::VerifyEntity(verifyer));
    if (!Akonadi2::VerifyEntityBuffer(verifyer)) {
        qWarning() << "invalid buffer";
    } else {
        mEntity = Akonadi2::GetEntity(dataValue);
    }
}

bool EntityBuffer::isValid() const
{
    return mEntity;
}

const Akonadi2::Entity &EntityBuffer::entity()
{
    return *mEntity;
}

const uint8_t* EntityBuffer::resourceBuffer()
{
    if (!mEntity) {
        qDebug() << "no buffer";
        return nullptr;
    }
    return mEntity->resource()->Data();
}

const uint8_t* EntityBuffer::metadataBuffer()
{
    if (!mEntity) {
        return nullptr;
    }
    return mEntity->metadata()->Data();
}

const uint8_t* EntityBuffer::localBuffer()
{
    if (!mEntity) {
        return nullptr;
    }
    return mEntity->local()->Data();
}

void EntityBuffer::extractResourceBuffer(void *dataValue, int dataSize, const std::function<void(const uint8_t *, size_t size)> &handler)
{
    Akonadi2::EntityBuffer buffer(dataValue, dataSize);
    if (auto resourceData = buffer.entity().resource()) {
        handler(resourceData->Data(), resourceData->size());
    }
}

flatbuffers::Offset<flatbuffers::Vector<uint8_t> > EntityBuffer::appendAsVector(flatbuffers::FlatBufferBuilder &fbb, void const *data, size_t size)
{
    //Since we do memcpy trickery, this will only work on little endian
    assert(FLATBUFFERS_LITTLEENDIAN);
    uint8_t *rawDataPtr = Q_NULLPTR;
    const auto pos = fbb.CreateUninitializedVector<uint8_t>(size, &rawDataPtr);
    std::memcpy((void*)rawDataPtr, data, size);
    return pos;
}

void EntityBuffer::assembleEntityBuffer(flatbuffers::FlatBufferBuilder &fbb, void const *metadataData, size_t metadataSize, void const *resourceData, size_t resourceSize, void const *localData, size_t localSize)
{
    auto metadata = appendAsVector(fbb, metadataData, metadataSize);
    auto resource = appendAsVector(fbb, resourceData, resourceSize);
    auto local = appendAsVector(fbb, localData, localSize);
    auto entity = Akonadi2::CreateEntity(fbb, metadata, resource, local);
    Akonadi2::FinishEntityBuffer(fbb, entity);
}

