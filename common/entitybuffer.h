#pragma once

#include <functional>
#include <flatbuffers/flatbuffers.h>

namespace Akonadi2 {
class Entity;

class EntityBuffer {
public:
    EntityBuffer(void *dataValue, int size);
    const flatbuffers::Vector<uint8_t> *resourceBuffer();
    const flatbuffers::Vector<uint8_t> *metadataBuffer();
    const flatbuffers::Vector<uint8_t> *localBuffer();
    const Entity &entity();

    static void extractResourceBuffer(void *dataValue, int dataSize, const std::function<void(const flatbuffers::Vector<uint8_t> *)> &handler);
    static void assembleEntityBuffer(flatbuffers::FlatBufferBuilder &fbb, void *metadataData, size_t metadataSize, void *resourceData, size_t resourceSize, void *localData, size_t localSize);

private:
    const Entity *mEntity;
};

}

