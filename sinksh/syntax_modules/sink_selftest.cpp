/*
 *   Copyright (C) 2018 Christian Mollekopf <mollekopf@kolabsys.com>
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

#include <QCoreApplication>
#include <QDebug>
#include <QObject> // tr()
#include <QModelIndex>
#include <QTime>

#include "common/resource.h"
#include "common/storage.h"
#include "common/resourceconfig.h"
#include "common/log.h"
#include "common/storage.h"
#include "common/definitions.h"
#include "common/store.h"
#include "common/propertyparser.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkSelfTest
{

bool selfTest(const QStringList &args_, State &state)
{
    using namespace Sink::ApplicationDomain;
    auto options = SyntaxTree::parseOptions(args_);
    if (options.positionalArguments.contains("stresstest")) {
        auto resource = SinkshUtils::parseUid(options.options.value("resource").first().toUtf8());
        qWarning() << "Stresstest on resource: " << resource;
        auto models = QSharedPointer<QList<QSharedPointer<QAbstractItemModel>>>::create();

        //Simulate the maillist, where we scroll down and trigger lots of fetchMore calls
        {
            Sink::Query query;
            query.resourceFilter(resource);
            query.limit(100);
            query.request<Mail::Subject>();
            query.request<Mail::Sender>();
            query.request<Mail::To>();
            query.request<Mail::Cc>();
            query.request<Mail::Bcc>();
            query.request<Mail::Date>();
            query.request<Mail::Unread>();
            query.request<Mail::Important>();
            query.request<Mail::Draft>();
            query.request<Mail::Sent>();
            query.request<Mail::Trash>();
            query.request<Mail::Folder>();
            query.sort<Mail::Date>();
            query.reduce<Mail::ThreadId>(Sink::Query::Reduce::Selector::max<Mail::Date>())
                .count("count")
                .collect<Mail::Unread>("unreadCollected")
                .collect<Mail::Important>("importantCollected");

            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
            models->append(model);
            QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [models, model, &state](const QModelIndex &start, const QModelIndex &end, const QVector<int> &roles) {
                if (roles.contains(Sink::Store::ChildrenFetchedRole)) {
                    if (!model->canFetchMore({})) {
                        qWarning() << "Model complete: " << models->count();
                        models->removeAll(model);
                    } else {
                        qWarning() << "Fetching more";
                        //Simulate superfluous fetchMore calls
                        for (int i = 0; i < 10; i++) {
                            model->fetchMore({});
                        }
                        return;
                    }
                    if (models->isEmpty()) {
                        state.commandFinished();
                    }
                }
            });

        }

        //Simluate lot's of mailviewers doing a bunch of queries
        {
            Sink::Query query;
            query.resourceFilter(resource);
            query.limit(10);
            query.request<Mail::Subject>();
            query.request<Mail::Sender>();
            query.request<Mail::To>();
            query.request<Mail::Cc>();
            query.request<Mail::Bcc>();
            query.request<Mail::Date>();
            query.request<Mail::Unread>();
            query.request<Mail::Important>();
            query.request<Mail::Draft>();
            query.request<Mail::Sent>();
            query.request<Mail::Trash>();
            query.request<Mail::Folder>();
            query.sort<Sink::ApplicationDomain::Mail::Date>();
            query.bloom<Sink::ApplicationDomain::Mail::ThreadId>();

            for (int i = 0; i < 50; i++) {
                auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
                *models << model;
                QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [models, model, &state](const QModelIndex &start, const QModelIndex &end, const QVector<int> &roles) {
                    if (roles.contains(Sink::Store::ChildrenFetchedRole)) {
                        models->removeAll(model);
                        qWarning() << "Model complete: " << models->count();
                        if (models->isEmpty()) {
                            state.commandFinished();
                        }
                    }
                        });
            }
        }

        return true;
    }

    state.printLine("Looking for resource plugins:");
    if (!Sink::ResourceFactory::load("sink.imap")) {
        state.printLine("Error: Failed to load the imap resource", 1);
    } else {
        state.printLine("Success: Managed to load the imap resource", 1);
    }

    return false;
}

Syntax::List syntax()
{
    Syntax syntax("selftest", QObject::tr("Selftext."), &SinkSelfTest::selfTest, Syntax::EventDriven);
    return Syntax::List() << syntax;
}

REGISTER_SYNTAX(SinkSelfTest)

}
