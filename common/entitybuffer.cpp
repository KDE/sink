#include "entitybuffer.h"

#include "entity_generated.h"
#include "metadata_generated.h"
#include <QDebug>

using namespace Akonadi2;

EntityBuffer::EntityBuffer(void *dataValue, int dataSize)
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

void EntityBuffer::assembleEntityBuffer(flatbuffers::FlatBufferBuilder &fbb, void *metadataData, size_t metadataSize, void *resourceData, size_t resourceSize, void *localData, size_t localSize)
{
    auto metadata = fbb.CreateVector<uint8_t>(static_cast<uint8_t*>(metadataData), metadataSize);
    auto resource = fbb.CreateVector<uint8_t>(static_cast<uint8_t*>(resourceData), resourceSize);
    auto local = fbb.CreateVector<uint8_t>(static_cast<uint8_t*>(localData), localSize);
    auto builder = Akonadi2::EntityBuilder(fbb);
    builder.add_metadata(metadata);
    builder.add_resource(resource);
    builder.add_local(local);

    auto buffer = builder.Finish();
    Akonadi2::FinishEntityBuffer(fbb, buffer);
}

