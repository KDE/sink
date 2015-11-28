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

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>

#include "common/clientapi.h"
#include "common/resource.h"
#include "common/storage.h"
#include "common/domain/event.h"
#include "common/domain/folder.h"
#include "common/resourceconfig.h"
#include "common/log.h"
#include "console.h"

#include <QWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QItemSelectionModel>

template <typename T>
class View : public QWidget
{
public:
    View(QAbstractItemModel *model)
        : QWidget()
    {
        auto modelView = new QTreeView(this);
        modelView->setModel(model);
        resize(1000, 1500);

        auto topLayout = new QVBoxLayout(this);

        auto titleLabel = new QLabel(this);
        titleLabel->setText("Demo");
        auto font = titleLabel->font();
        font.setWeight(QFont::Bold);
        titleLabel->setFont(font);
        titleLabel->setAlignment(Qt::AlignCenter);

        auto syncButton = new QPushButton(this);
        syncButton->setText("Synchronize!");
        QObject::connect(syncButton, &QPushButton::pressed, []() {
            Akonadi2::Query query;
            query.resources << "org.kde.dummy.instance1";
            query.syncOnDemand = true;
            Akonadi2::Store::synchronize(query).exec();
        });

        auto removeButton = new QPushButton(this);
        removeButton->setText("Remove");
        QObject::connect(removeButton, &QPushButton::pressed, [modelView]() {
            for (auto index : modelView->selectionModel()->selectedIndexes()) {
                auto object = index.data(Akonadi2::Store::DomainObjectRole).value<typename T::Ptr>();
                Akonadi2::Store::remove(*object).exec();
            }
        });

        topLayout->addWidget(titleLabel);
        topLayout->addWidget(syncButton);
        topLayout->addWidget(modelView, 10);

        show();
    }

};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCommandLineParser cliOptions;
    cliOptions.addPositionalArgument(QObject::tr("[resource]"),
                                     QObject::tr("A resource to connect to"));
    cliOptions.addOption(QCommandLineOption("clear"));
    cliOptions.addOption(QCommandLineOption("debuglevel"));
    cliOptions.addOption(QCommandLineOption("list"));
    cliOptions.addOption(QCommandLineOption("count"));
    cliOptions.addHelpOption();
    cliOptions.process(app);
    QStringList resources = cliOptions.positionalArguments();
    if (resources.isEmpty()) {
        resources << "org.kde.dummy.instance1";
    }

    if (cliOptions.isSet("clear")) {
        qDebug() << "Clearing";
        for (const auto &resource : resources) {
            Akonadi2::Storage store(Akonadi2::Store::storageLocation(), resource, Akonadi2::Storage::ReadWrite);
            store.removeFromDisk();
        }
        return 0;
    }
    if (cliOptions.isSet("debuglevel")) {
        Akonadi2::Log::setDebugOutputLevel(static_cast<Akonadi2::Log::DebugLevel>(cliOptions.value("debuglevel").toInt()));
    }

    //Ensure resource is ready
    for (const auto &resource : resources) {
        Akonadi2::ResourceFactory::load(Akonadi2::Store::resourceName(resource.toLatin1()));
        ResourceConfig::addResource(resource.toLatin1(), Akonadi2::Store::resourceName(resource.toLatin1()));
    }

    Akonadi2::Query query;
    for (const auto &res : resources) {
        query.resources << res.toLatin1();
    }
    query.syncOnDemand = false;
    query.processAll = false;
    query.requestedProperties << "name";
    query.liveQuery = true;

    auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
    if (cliOptions.isSet("list")) {
        query.liveQuery = false;
        qDebug() << "Listing";
        QObject::connect(model.data(), &QAbstractItemModel::rowsInserted, [model](const QModelIndex &index, int start, int end) {
            for (int i = start; i <= end; i++) {
                qDebug() << model->data(model->index(i, 0, index)).toString();
            }
        });
        return app.exec();
    } else if (cliOptions.isSet("count")) {
        query.liveQuery = false;
        qDebug() << "Counted results " << model->rowCount(QModelIndex());
    } else {
        query.liveQuery = true;
        auto view = QSharedPointer<View<Akonadi2::ApplicationDomain::Folder> >::create(model.data());
        return app.exec();
    }
    return 0;
}
