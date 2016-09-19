#pragma once

#include "sink_export.h"
#include <functional>
#include <flatbuffers/flatbuffers.h>
#include "metadata_generated.h"
#include <QByteArray>

namespace Sink {
struct Entity;

class SINK_EXPORT EntityBuffer
{
public:
    EntityBuffer(const void *dataValue, int size);
    EntityBuffer(const QByteArray &data);
    const uint8_t *resourceBuffer();
    const uint8_t *metadataBuffer();
    const uint8_t *localBuffer();
    const Entity &entity() const;
    bool isValid() const;

    Sink::Operation operation() const;
    qint64 revision() const;

    static void extractResourceBuffer(void *dataValue, int dataSize, const std::function<void(const uint8_t *, size_t size)> &handler);
    /*
     * TODO: Ideally we would be passing references to vectors in the same bufferbuilder, to avoid needlessly copying data.
     * Unfortunately I couldn't find a way to cast a table to a vector<uint8_t> reference.
     * We can't use union's either (which would allow to have a field that stores a selection of tables), as we don't want to modify
     * the entity schema for each resource's buffers.
     */
    static void assembleEntityBuffer(
        flatbuffers::FlatBufferBuilder &fbb, void const *metadataData, size_t metadataSize, void const *resourceData, size_t resourceSize, void const *localData, size_t localSize);
    static flatbuffers::Offset<flatbuffers::Vector<uint8_t>> appendAsVector(flatbuffers::FlatBufferBuilder &fbb, void const *data, size_t size);
    template <typename T>
    static const T *readBuffer(const uint8_t *data, int size)
    {
        flatbuffers::Verifier verifier(data, size);
        if (verifier.VerifyBuffer<T>(nullptr)) {
            return flatbuffers::GetRoot<T>(data);
        }
        return nullptr;
    }

    template <typename T>
    static const T *readBuffer(const flatbuffers::Vector<uint8_t> *data)
    {
        if (data) {
            return readBuffer<T>(data->Data(), data->size());
        }
        return nullptr;
    }


private:
    const Entity *mEntity;
};
}
