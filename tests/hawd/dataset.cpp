/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include "dataset.h"

#include <QDateTime>
#include <QDebug>

#include <iostream>

namespace HAWD
{

static const QString s_annotationKey("__annotation__");

Dataset::Row::Row(const Row &other)
    : m_key(other.m_key),
      m_columns(other.m_columns),
      m_data(other.m_data),
      m_dataset(other.m_dataset)
{
}

Dataset::Row::Row(const Dataset &dataset, qint64 key)
    : m_key(key),
      m_columns(dataset.definition().columns()),
      m_dataset(&dataset)
{
    // TODO: pre-populate m_data, or do that on buffer creation?
    QHashIterator<QString, DataDefinition> it(dataset.definition().columns());
    while (it.hasNext()) {
        it.next();
        m_data.insert(it.key(), QVariant());
    }
}

Dataset::Row &Dataset::Row::operator=(const Row &rhs)
{
    m_key = rhs.m_key;
    m_columns = rhs.m_columns;
    m_data = rhs.m_data;
    m_dataset = rhs.m_dataset;
    return *this;
}

void Dataset::Row::setValue(const QString &column, const QVariant &value)
{
    if (!m_columns.contains(column) || !value.canConvert(m_columns[column].type())) {
        return;
    }

    m_data[column] = value;
}

void Dataset::Row::annotate(const QString &note)
{
    m_annotation = note;
}

qint64 Dataset::Row::key() const
{
    if (m_key < 1) {
        const_cast<Dataset::Row *>(this)->m_key = QDateTime::currentMSecsSinceEpoch();
    }

    return m_key;
}

void Dataset::Row::fromBinary(QByteArray &data)
{
    QVariant value;
    QString key;
    QDataStream stream(&data, QIODevice::ReadOnly);

    while (!stream.atEnd()) {
        stream >> key >> value;
        if (key == s_annotationKey) {
            m_annotation = value.toString();
        } else {
            setValue(key, value);
        }
    }
}

QByteArray Dataset::Row::toBinary() const
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    QHashIterator<QString, QVariant> it(m_data);
    while (it.hasNext()) {
        it.next();
        stream << it.key() << it.value();
    }

    if (!m_annotation.isEmpty()) {
        stream << s_annotationKey << m_annotation;
    }
    return data;
}

QString Dataset::Row::toString() const
{
    if (m_data.isEmpty()) {
        return QString();
    }

    QString string;
    QHashIterator<QString, QVariant> it(m_data);
    while (it.hasNext()) {
        it.next();
        string.append('\t').append(it.value().toString());
    }

    if (!m_annotation.isEmpty()) {
        string.append('\t').append(m_annotation);
    }

    return string;
}

Dataset::Dataset(const QString &name, const State &state)
    : m_definition(state.datasetDefinition(name)),
      m_storage(state.resultsPath(), m_definition.name(), Storage::ReadWrite)
{
    //TODO: it should use a different file name if the data columns have changed
    m_storage.startTransaction();
}

Dataset::~Dataset()
{
    m_storage.commitTransaction();
}

bool Dataset::isValid()
{
    return m_definition.isValid();
}

const DatasetDefinition &Dataset::definition() const
{
    return m_definition;
}

qint64 Dataset::insertRow(const Row &row)
{
    if (row.m_dataset != this) {
        return 0;
    }

    qint64 key = row.key();
    QByteArray data = row.toBinary();
    m_storage.write((const char *)&key, sizeof(qint64), data.constData(), data.size());
    return key;
}

void Dataset::removeRow(const Row &row)
{
    //TODO
}

Dataset::Row Dataset::row(qint64 key)
{
    if (key < 1) {
        return Row(*this);
    }

    Row row(*this, key);
    m_storage.read((const char *)&key, sizeof(qint64),
            [&row](void *ptr, int size) -> bool {
                QByteArray array((const char*)ptr, size);
                row.fromBinary(array);
                return true;
            },
            Storage::basicErrorHandler()
            );
    return row;
}

Dataset::Row Dataset::lastRow()
{
    //TODO
    return Row(*this);
}

} // namespace HAWD

