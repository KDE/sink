#pragma once

#include <flatbuffers/flatbuffers.h>
#include <QByteArray>

namespace Sink {
namespace BufferUtils {
    template<typename T>
    static QByteArray extractBuffer(const T *data)
    {
        return QByteArray::fromRawData(reinterpret_cast<char const *>(data->Data()), data->size());
    }

    template<typename T>
    static QByteArray extractBufferCopy(const T *data)
    {
        return QByteArray(reinterpret_cast<char const *>(data->Data()), data->size());
    }

    static QByteArray extractBuffer(const flatbuffers::FlatBufferBuilder &fbb)
    {
        return QByteArray::fromRawData(reinterpret_cast<char const *>(fbb.GetBufferPointer()), fbb.GetSize());
    }
}
}

