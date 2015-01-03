/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include "resourcefactory.h"
#include "facade.h"
#include "entitybuffer.h"
#include "pipeline.h"
#include "dummycalendar_generated.h"
#include "metadata_generated.h"
#include "domainadaptor.h"
#include <QUuid>

/*
 * Figure out how to implement various classes of processors:
 * * read-only (index and such) => domain adapter
 * * filter => provide means to move entity elsewhere, and also reflect change in source (I guess?)
 * * flag extractors? => like read-only? Or write to local portion of buffer?
 * ** $ISSPAM should become part of domain object and is written to the local part of the mail. 
 * ** => value could be calculated by the server directly
 */
// template <typename DomainType>
class SimpleProcessor : public Akonadi2::Preprocessor
{
public:
    SimpleProcessor(const std::function<void(const Akonadi2::PipelineState &state, const Akonadi2::Entity &e)> &f)
        : Akonadi2::Preprocessor(),
        mFunction(f)
    {
    }

    void process(const Akonadi2::PipelineState &state, const Akonadi2::Entity &e) {
        mFunction(state, e);
        processingCompleted(state);
    }

protected:
    std::function<void(const Akonadi2::PipelineState &state, const Akonadi2::Entity &e)> mFunction;
};

// template <typename DomainType>
// class SimpleReadOnlyProcessor : public SimpleProcessor<DomainType>
// {
// public:
//     using SimpleProcessor::SimpleProcessor;
//     void process(Akonadi2::PipelineState state) {
//         mFunction();
//     }
// };


static std::string createEvent()
{
    static const size_t attachmentSize = 1024*2; // 2KB
    static uint8_t rawData[attachmentSize];
    static flatbuffers::FlatBufferBuilder fbb;
    fbb.Clear();
    {
        auto summary = fbb.CreateString("summary");
        auto data = fbb.CreateUninitializedVector<uint8_t>(attachmentSize);
        DummyCalendar::DummyEventBuilder eventBuilder(fbb);
        eventBuilder.add_summary(summary);
        eventBuilder.add_attachment(data);
        auto eventLocation = eventBuilder.Finish();
        DummyCalendar::FinishDummyEventBuffer(fbb, eventLocation);
        memcpy((void*)DummyCalendar::GetDummyEvent(fbb.GetBufferPointer())->attachment()->Data(), rawData, attachmentSize);
    }

    return std::string(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
}

QMap<QString, QString> populate()
{
    QMap<QString, QString> content;
    for (int i = 0; i < 2; i++) {
        auto event = createEvent();
        content.insert(QString("key%1").arg(i), QString::fromStdString(event));
    }
    return content;
}

static QMap<QString, QString> s_dataSource = populate();

DummyResource::DummyResource()
    : Akonadi2::Resource()
{
}

void DummyResource::configurePipeline(Akonadi2::Pipeline *pipeline)
{
    auto eventFactory = QSharedPointer<DummyEventAdaptorFactory>::create();
    //TODO setup preprocessors for each domain type and pipeline type allowing full customization
    //Eventually the order should be self configuring, for now it's hardcoded.
    auto eventIndexer = new SimpleProcessor([eventFactory](const Akonadi2::PipelineState &state, const Akonadi2::Entity &entity) {
        auto adaptor = eventFactory->createAdaptor(entity);
        qDebug() << "Summary preprocessor: " << adaptor->getProperty("summary").toString();
    });
    pipeline->setPreprocessors<Akonadi2::Domain::Event>(Akonadi2::Pipeline::NewPipeline, QVector<Akonadi2::Preprocessor*>() << eventIndexer);
}

void findByRemoteId(QSharedPointer<Akonadi2::Storage> storage, const QString &rid, std::function<void(void *keyValue, int keySize, void *dataValue, int dataSize)> callback)
{
    //TODO lookup in rid index instead of doing a full scan
    const std::string ridString = rid.toStdString();
    storage->scan("", [&](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
        if (QByteArray::fromRawData(static_cast<char*>(keyValue), keySize).startsWith("__internal")) {
            return true;
        }

        Akonadi2::EntityBuffer::extractResourceBuffer(dataValue, dataSize, [&](const flatbuffers::Vector<uint8_t> *buffer) {
            flatbuffers::Verifier verifier(buffer->Data(), buffer->size());
            if (DummyCalendar::VerifyDummyEventBuffer(verifier)) {
                DummyCalendar::DummyEvent const *resourceBuffer = DummyCalendar::GetDummyEvent(buffer->Data());
                if (resourceBuffer && resourceBuffer->remoteId()) {
                    if (std::string(resourceBuffer->remoteId()->c_str(), resourceBuffer->remoteId()->size()) == ridString) {
                        callback(keyValue, keySize, dataValue, dataSize);
                    }
                }
            }
        });
        return true;
    });
}

Async::Job<void> DummyResource::synchronizeWithSource(Akonadi2::Pipeline *pipeline)
{
    return Async::start<void>([this, pipeline](Async::Future<void> &f) {
        //TODO use a read-only transaction during the complete sync to sync against a defined revision
        auto storage = QSharedPointer<Akonadi2::Storage>::create(Akonadi2::Store::storageLocation(), "org.kde.dummy");
        for (auto it = s_dataSource.constBegin(); it != s_dataSource.constEnd(); it++) {
            bool isNew = true;
            if (storage->exists()) {
                findByRemoteId(storage, it.key(), [&](void *keyValue, int keySize, void *dataValue, int dataSize) {
                    isNew = false;
                });
            }
            if (isNew) {
                m_fbb.Clear();

                const QByteArray data = it.value().toUtf8();
                auto eventBuffer = DummyCalendar::GetDummyEvent(data.data());

                //Map the source format to the buffer format (which happens to be an exact copy here)
                auto summary = m_fbb.CreateString(eventBuffer->summary()->c_str());
                auto rid = m_fbb.CreateString(it.key().toStdString().c_str());
                auto description = m_fbb.CreateString(it.key().toStdString().c_str());
                static uint8_t rawData[100];
                auto attachment = m_fbb.CreateVector(rawData, 100);

                auto builder = DummyCalendar::DummyEventBuilder(m_fbb);
                builder.add_summary(summary);
                builder.add_remoteId(rid);
                builder.add_description(description);
                builder.add_attachment(attachment);
                auto buffer = builder.Finish();
                DummyCalendar::FinishDummyEventBuffer(m_fbb, buffer);
                //TODO toRFC4122 would probably be more efficient, but results in non-printable keys.
                const auto key = QUuid::createUuid().toString().toUtf8();
                pipeline->newEntity<Akonadi2::Domain::Event>(key, m_fbb.GetBufferPointer(), m_fbb.GetSize());
            } else { //modification
                //TODO diff and create modification if necessary
            }
        }
        //TODO find items to remove
        f.setFinished();
    });
}

void DummyResource::processCommand(int commandId, const QByteArray &data, uint size, Akonadi2::Pipeline *pipeline)
{
    Q_UNUSED(commandId)
    Q_UNUSED(data)
    Q_UNUSED(size)
    //TODO reallly process the commands :)
    auto builder = DummyCalendar::DummyEventBuilder(m_fbb);
    builder .add_summary(m_fbb.CreateString("summary summary!"));
    auto buffer = builder.Finish();
    DummyCalendar::FinishDummyEventBuffer(m_fbb, buffer);
    pipeline->newEntity<Akonadi2::Domain::Event>("fakekey", m_fbb.GetBufferPointer(), m_fbb.GetSize());
    m_fbb.Clear();
}

DummyResourceFactory::DummyResourceFactory(QObject *parent)
    : Akonadi2::ResourceFactory(parent)
{

}

Akonadi2::Resource *DummyResourceFactory::createResource()
{
    return new DummyResource();
}

void DummyResourceFactory::registerFacades(Akonadi2::FacadeFactory &factory)
{
    factory.registerFacade<Akonadi2::Domain::Event, DummyResourceFacade>(PLUGIN_NAME);
}

