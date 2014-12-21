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
#include "dummycalendar_generated.h"
#include <QUuid>

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

void findByRemoteId(QSharedPointer<Akonadi2::Storage> storage, const QString &rid, std::function<void(void *keyValue, int keySize, void *dataValue, int dataSize)> callback)
{
    //TODO lookup in rid index instead of doing a full scan
    const std::string ridString = rid.toStdString();
    storage->scan("", [&](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
        auto eventBuffer = DummyCalendar::GetDummyEvent(dataValue);
        if (std::string(eventBuffer->remoteId()->c_str(), eventBuffer->remoteId()->size()) == ridString) {
            callback(keyValue, keySize, dataValue, dataSize);
        }
        return true;
    });
}

void DummyResource::synchronizeWithSource(Akonadi2::Pipeline *pipeline)
{
    //TODO use a read-only transaction during the complete sync to sync against a defined revision
    
    qDebug() << "synchronize with source";

    auto storage = QSharedPointer<Akonadi2::Storage>::create(Akonadi2::Store::storageLocation(), "org.kde.dummy");
    for (auto it = s_dataSource.constBegin(); it != s_dataSource.constEnd(); it++) {
        bool isNew = true;
        if (storage->exists()) {
            findByRemoteId(storage, it.key(), [&](void *keyValue, int keySize, void *dataValue, int dataSize) {
                isNew = false;
            });
        }

        if (isNew) {
            //TODO: perhaps it would be more convenient to populate the domain types?
            //Resource specific parts are not accessible that way, but then we would only have to implement the property mapping in one place
            const QByteArray data = it.value().toUtf8();
            auto eventBuffer = DummyCalendar::GetDummyEvent(data.data());

            //Map the source format to the buffer format (which happens to be an exact copy here)
            auto builder = DummyCalendar::DummyEventBuilder(m_fbb);
            builder.add_summary(m_fbb.CreateString(eventBuffer->summary()->c_str()));
            auto buffer = builder.Finish();
            DummyCalendar::FinishDummyEventBuffer(m_fbb, buffer);

            //TODO toRFC4122 would probably be more efficient, but results in non-printable keys.
            const auto key = QUuid::createUuid().toString().toUtf8();
            //TODO can we really just start populating the buffer and pass the buffer builder?
            qDebug() << "new event";
            pipeline->newEntity(key, m_fbb);
        } else { //modification
            //TODO diff and create modification if necessary
        }
    }
    //TODO find items to remove
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
    pipeline->newEntity("fakekey", m_fbb);
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

