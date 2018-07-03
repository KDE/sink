#pragma once

#include <flatbuffers/flatbuffers.h>
#include <QByteArray>
#include <QList>

namespace Sink {
namespace BufferUtils {

//Does not copy the data
template <typename T>
static QByteArray extractBuffer(const T *data)
{
    return QByteArray::fromRawData(reinterpret_cast<char const *>(data->Data()), data->size());
}

//Returns a deep copy of the data
template <typename T>
static QByteArray extractBufferCopy(const T *data)
{
    return QByteArray(reinterpret_cast<char const *>(data->Data()), data->size());
}

//Does not copy the data
static QByteArray extractBuffer(const flatbuffers::FlatBufferBuilder &fbb)
{
    return QByteArray::fromRawData(reinterpret_cast<char const *>(fbb.GetBufferPointer()), fbb.GetSize());
}

//Returns a deep copy of the data
static QList<QByteArray> fromVector(const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> &vector)
{
    QList<QByteArray> list;
    for (const flatbuffers::String *data : vector) {
        Q_ASSERT(data);
        list << QByteArray::fromStdString(data->str());
    }
    return list;
}

template <typename T>
static flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>> toVector(flatbuffers::FlatBufferBuilder &fbb, const T &list)
{
    std::vector<flatbuffers::Offset<flatbuffers::String>> modifiedPropertiesList;
    for (const auto &change : list) {
        auto s = fbb.CreateString(change.toStdString());
        modifiedPropertiesList.push_back(s);
    }
    return fbb.CreateVector(modifiedPropertiesList);
}

}
}
