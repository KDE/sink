/*
 *   Copyright (C) 2017 Sandro Knauß <knauss@kolabsys.com>
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

#include "contactpreprocessor.h"

#include <KContacts/VCardConverter>
#include <KContacts/Addressee>

using namespace Sink;

SINK_DEBUG_AREA("contactpreprocessor")

void updatedProperties(Sink::ApplicationDomain::Contact &contact, const KContacts::Addressee &addressee)
{
    contact.setUid(addressee.uid());
    contact.setFn(addressee.formattedName());
    QByteArrayList emails;
    for (const auto email : addressee.emails()) {
        emails << email.toUtf8();
    }
    contact.setEmails(emails);
}

ContactPropertyExtractor::~ContactPropertyExtractor()
{
}

void ContactPropertyExtractor::newEntity(Sink::ApplicationDomain::Contact &contact)
{
    KContacts::VCardConverter converter;
    const auto addressee = converter.parseVCard(contact.getVcard());
    if (!addressee.isEmpty()) {
        updatedProperties(contact, addressee);
    }
}

void ContactPropertyExtractor::modifiedEntity(const Sink::ApplicationDomain::Contact &oldContact, Sink::ApplicationDomain::Contact &newContact)
{
    KContacts::VCardConverter converter;
    const auto addressee = converter.parseVCard(newContact.getVcard());
    if (!addressee.isEmpty()) {
        updatedProperties(newContact, addressee);
    }
}
