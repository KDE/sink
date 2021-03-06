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
#include "sink_export.h"

#include "pipeline.h"

namespace SpecialPurpose {
    bool SINK_EXPORT isSpecialPurposeFolderName(const QString &name);
    QByteArray SINK_EXPORT getSpecialPurposeType(const QString &name);
}

class SINK_EXPORT SpecialPurposeProcessor : public Sink::Preprocessor
{
public:
    SpecialPurposeProcessor();

    void newEntity(Sink::ApplicationDomain::ApplicationDomainType &newEntity) Q_DECL_OVERRIDE;
    void modifiedEntity(const Sink::ApplicationDomain::ApplicationDomainType &oldEntity, Sink::ApplicationDomain::ApplicationDomainType &newEntity) Q_DECL_OVERRIDE;

private:
    QByteArray findFolder(const QByteArray &specialPurpose, bool createIfMissing = false);
    void moveToFolder(Sink::ApplicationDomain::ApplicationDomainType &newEntity);
    bool isSpecialPurposeFolder(const QByteArray &folder) const;

    QHash<QByteArray, QByteArray> mSpecialPurposeFolders;
};
