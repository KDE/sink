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

#pragma once

#include "datasetdefinition.h"

#include "hawd_export.h"
#include "state.h"
#include "common/storage.h"

#include <QHash>
#include <QVariant>

namespace HAWD
{

class HAWD_EXPORT Dataset
{
public:
    class Row
    {
        public:
            enum StandardCols {
                Annotation,
                CommitHash,
                Timestamp,
                All = Annotation | CommitHash | Timestamp
            };
            Row(const Row &other);
            Row &operator=(const Row &rhs);
            void setValue(const QString &column, const QVariant &value);
            QVariant value(const QString &column);
            void annotate(const QString &note);
            void setCommitHash(const QString &hash);
            qint64 key() const;
            QByteArray toBinary() const;
            QString toString(const QStringList &cols = QStringList(), int standardCols = All, const QString &seperator = "\t") const;

        private:
            Row();
            Row(const Dataset &dataset, qint64 key = 0);
            void fromBinary(QByteArray &binary);

            qint64 m_key;
            QHash<QString, DataDefinition> m_columns;
            QHash<QString, QVariant> m_data;
            QString m_annotation;
            QString m_commitHash;
            const Dataset *m_dataset;
            friend class Dataset;
    };

    Dataset(const QString &name, const State &state);
    ~Dataset();

    bool isValid() const;
    const DatasetDefinition &definition() const;
    QString tableHeaders(const QStringList &cols = QStringList(), int standardCols = Row::All, const QString &seperator = "\t") const;

    qint64 insertRow(const Row &row);
    void removeRow(const Row &row);
    void eachRow(const std::function<void(const Row &row)> &resultHandler);
    Row row(qint64 key = 0);
    Row lastRow();
    //TODO: row cursor

private:
    DatasetDefinition m_definition;
    Akonadi2::Storage m_storage;
    QString m_commitHash;
};

} // namespace HAWD

