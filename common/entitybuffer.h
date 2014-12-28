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

    static void extractResourceBuffer(void *dataValue, int dataSize, const std::function<void(const flatbuffers::Vector<uint8_t> *)> &handler);
private:
    const Entity *mEntity;
};

}

