#pragma once

#include <functional>
#include <flatbuffers/flatbuffers.h>

namespace Akonadi2 {
class Entity;

class EntityBuffer {
public:
    EntityBuffer(void *dataValue, int size);
    const uint8_t *resourceBuffer();
    const uint8_t *metadataBuffer();
    const uint8_t *localBuffer();
    const Entity &entity();

    static void extractResourceBuffer(void *dataValue, int dataSize, const std::function<void(const uint8_t *, size_t size)> &handler);
    static void assembleEntityBuffer(flatbuffers::FlatBufferBuilder &fbb, void const *metadataData, size_t metadataSize, void const *resourceData, size_t resourceSize, void const *localData, size_t localSize);

private:
    const Entity *mEntity;
};

}

