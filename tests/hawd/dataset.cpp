/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dataset.h"

#include <QDateTime>
#include <QDebug>
#include <QDataStream>

#include <iostream>

namespace HAWD
{

static const QString s_annotationKey("__annotation__");
static const QString s_hashKey("__commithash__");

Dataset::Row::Row(const Row &other)
    : m_key(other.m_key),
      m_columns(other.m_columns),
      m_data(other.m_data),
      m_annotation(other.m_annotation),
      m_commitHash(other.m_commitHash),
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
    m_annotation = rhs.m_annotation;
    m_commitHash = rhs.m_commitHash;
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

void Dataset::Row::setCommitHash(const QString &hash)
{
    m_commitHash = hash;
}

qint64 Dataset::Row::key() const
{
    if (m_key < 1) {
        const_cast<Dataset::Row *>(this)->m_key = QDateTime::currentMSecsSinceEpoch();
    }

    return m_key;
}

void Dataset::Row::fromBinary(QByteArray data)
{
    QVariant value;
    QString key;
    QDataStream stream(&data, QIODevice::ReadOnly);

    while (!stream.atEnd()) {
        stream >> key;
        if (stream.atEnd()) {
            break;
        }

        stream >> value;
        if (key == s_annotationKey) {
            m_annotation = value.toString();
        } else if (key == s_hashKey) {
            m_commitHash = value.toString();
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
        if (it.value().isValid()) {
            stream << it.key() << it.value();
        }
    }

    if (!m_commitHash.isEmpty()) {
        stream << s_hashKey << QVariant(m_commitHash);
    }

    if (!m_annotation.isEmpty()) {
        stream << s_annotationKey << QVariant(m_annotation);
    }

    return data;
}

QString Dataset::tableHeaders(const QStringList &cols, int standardCols, const QString &seperator) const
{
    if (!isValid()) {
        return QString();
    }

    QStringList strings;

    if (standardCols & Row::Timestamp) {
        strings << QObject::tr("Timestamp");
    }

    if (standardCols & Row::CommitHash) {
        strings << QObject::tr("Commit");
    }

    QHashIterator<QString, DataDefinition> it(m_definition.columns());
    while (it.hasNext()) {
        it.next();
        QString header = it.key();
        if (cols.isEmpty() || cols.contains(header)) {
            if (!it.value().unit().isEmpty()) {
                header.append(" (").append(it.value().unit()).append(")");
            }
            strings << header;
        }
    }

    if (standardCols & Row::Annotation) {
        strings << QObject::tr("Annotation");
    }

    return strings.join(seperator);
}

QString Dataset::Row::toString(const QStringList &cols, int standardCols, const QString &seperator) const
{
    if (m_data.isEmpty()) {
        return QString();
    }

    QStringList strings;

    if (standardCols & Timestamp) {
        strings << QString::number(m_key);
    }

    if (standardCols & CommitHash) {
        strings << m_commitHash;
    }

    QHashIterator<QString, QVariant> it(m_data);
    while (it.hasNext()) {
        it.next();
        if (cols.isEmpty() || cols.contains(it.key())) {
            strings << it.value().toString();
        }
    }

    if (standardCols & Annotation) {
        strings << m_annotation;
    }

    return strings.join(seperator);
}

Dataset::Dataset(const QString &name, const State &state)
    : m_definition(state.datasetDefinition(name)),
      m_storage(state.resultsPath(), name, Akonadi2::Storage::ReadWrite),
      m_commitHash(state.commitHash()),
      m_transaction(std::move(m_storage.createTransaction()))
{
}

Dataset::~Dataset()
{
    m_transaction.commit();
}

bool Dataset::isValid() const
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
    m_transaction.openDatabase().write(QByteArray::fromRawData((const char *)&key, sizeof(qint64)), row.toBinary());
    return key;
}

void Dataset::removeRow(const Row &row)
{
    //TODO
}

void Dataset::eachRow(const std::function<void(const Row &row)> &resultHandler)
{
    if (!isValid()) {
        return;
    }

    Row row(*this);
    m_transaction.openDatabase().scan("",
                   [&](const QByteArray &key, const QByteArray &value) -> bool {
                       if (key.size() != sizeof(qint64)) {
                           return true;
                       }

                       row.fromBinary(value);
                       row.m_key = *(qint64 *)key.data();
                       resultHandler(row);
                       return true;
                   },
                   Akonadi2::Storage::basicErrorHandler());
}

Dataset::Row Dataset::row(qint64 key)
{
    if (key < 1) {
        Row row(*this);
        row.setCommitHash(m_commitHash);
        return row;
    }

    Row row(*this, key);
    m_transaction.openDatabase().scan(QByteArray::fromRawData((const char *)&key, sizeof(qint64)),
            [&row](const QByteArray &key, const QByteArray &value) -> bool {
                row.fromBinary(value);
                return true;
            },
            Akonadi2::Storage::basicErrorHandler()
            );
    return row;
}

Dataset::Row Dataset::lastRow()
{
    //TODO
    return Row(*this);
}

} // namespace HAWD

