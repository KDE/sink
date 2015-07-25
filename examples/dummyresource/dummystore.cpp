/*
 *   Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include "dummystore.h"

#include <QString>

#include "dummycalendar_generated.h"

static std::string createEvent(int i)
{
    static const size_t attachmentSize = 1024*2; // 2KB
    static uint8_t rawData[attachmentSize];
    static flatbuffers::FlatBufferBuilder fbb;
    fbb.Clear();
    {
        uint8_t *rawDataPtr = nullptr;
        auto summary = fbb.CreateString("summary" + std::to_string(i));
        auto data = fbb.CreateUninitializedVector<uint8_t>(attachmentSize, &rawDataPtr);
        DummyCalendar::DummyEventBuilder eventBuilder(fbb);
        eventBuilder.add_summary(summary);
        eventBuilder.add_attachment(data);
        auto eventLocation = eventBuilder.Finish();
        DummyCalendar::FinishDummyEventBuffer(fbb, eventLocation);
        memcpy((void*)rawDataPtr, rawData, attachmentSize);
    }

    return std::string(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
}

QMap<QString, QString> populate()
{
    QMap<QString, QString> content;
    for (int i = 0; i < 2; i++) {
        auto event = createEvent(i);
        content.insert(QString("key%1").arg(i), QString::fromStdString(event));
    }
    return content;
}

static QMap<QString, QString> s_dataSource = populate();


QMap<QString, QString> DummyStore::data() const
{
    return s_dataSource;
}
