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

DummyResource::DummyResource()
    : Akonadi2::Resource()
{

}

void DummyResource::synchronizeWithSource(Akonadi2::Pipeline *pipeline)
{
    // TODO actually populate the storage with new items
    auto builder = DummyCalendar::DummyEventBuilder(m_fbb);
    builder .add_summary(m_fbb.CreateString("summary summary!"));
    auto buffer = builder.Finish();
    DummyCalendar::FinishDummyEventBuffer(m_fbb, buffer);
    pipeline->newEntity("fakekey", m_fbb);
    m_fbb.Clear();
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

