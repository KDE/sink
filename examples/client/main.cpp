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
#include <QElapsedTimer>

#include "common/clientapi.h"
#include "common/resource.h"
#include "common/storage.h"
#include "common/domain/event.h"
#include "common/domain/folder.h"
#include "common/resourceconfig.h"
#include "common/log.h"
#include "common/storage.h"
#include "common/definitions.h"
#include "console.h"

#include <QWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QItemSelectionModel>
#include <iostream>

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
        topLayout->addWidget(removeButton);
        topLayout->addWidget(modelView, 10);

        show();
    }

};

class MyApplication : public QApplication
{
    QElapsedTimer t;
public:
    MyApplication(int& argc, char ** argv) : QApplication(argc, argv) { }
    virtual ~MyApplication() { }

    virtual bool notify(QObject* receiver, QEvent* event)
    {
        t.start();
        bool ret = QApplication::notify(receiver, event);
        if(t.elapsed() > 3)
            qDebug("processing event type %d for object %s took %dms",
                (int)event->type(), receiver->objectName().toLocal8Bit().data(),
                (int)t.elapsed());
        return ret;
    }
};

static QSharedPointer<QAbstractItemModel> loadModel(const QString &type, Akonadi2::Query query)
{
    QTime time;
    time.start();

    QSharedPointer<QAbstractItemModel> model;
    if (type == "folder") {
        query.requestedProperties << "name" << "parent";
        model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
    } else if (type == "mail") {
        query.requestedProperties << "subject" << "folder" << "date";
        model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Mail>(query);
    } else if (type == "event") {
        query.requestedProperties << "summary";
        model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Event>(query);
    }
    qDebug() << "Folder type " << type;
    qDebug() << "Loaded model in " << time.elapsed() << " ms";
    return model;
}

int main(int argc, char *argv[])
{
    MyApplication app(argc, argv);

    QCommandLineParser cliOptions;
    cliOptions.addPositionalArgument(QObject::tr("[resource]"),
                                     QObject::tr("A resource to connect to"));
    cliOptions.addPositionalArgument(QObject::tr("[type]"),
                                     QObject::tr("A type to work with"));
    cliOptions.addOption(QCommandLineOption("clear"));
    cliOptions.addOption(QCommandLineOption("debuglevel"));
    // cliOptions.addOption(QCommandLineOption("type", "type to list", "type"));
    cliOptions.addOption(QCommandLineOption("list"));
    cliOptions.addOption(QCommandLineOption("count"));
    cliOptions.addOption(QCommandLineOption("synchronize"));
    cliOptions.addHelpOption();
    cliOptions.process(app);
    QStringList args = cliOptions.positionalArguments();
    auto type = args.takeLast();
    auto resources = args;
    if (resources.isEmpty()) {
        resources << "org.kde.dummy.instance1";
    }

    if (cliOptions.isSet("clear")) {
        qDebug() << "Clearing";
        for (const auto &resource : resources) {
            Akonadi2::Store::removeFromDisk(resource.toLatin1());
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
    query.liveQuery = true;

    qDebug() << "Type: " << type;

    if (cliOptions.isSet("list")) {
        query.liveQuery = false;
        auto model = loadModel(type, query);
        qDebug() << "Listing";
        std::cout << "\tColumn\t ";
        for (int i = 0; i < model->columnCount(QModelIndex()); i++) {
            std::cout << "\t" << model->headerData(i, Qt::Horizontal).toString().toStdString();
        }
        std::cout << std::endl;
        QObject::connect(model.data(), &QAbstractItemModel::rowsInserted, [model](const QModelIndex &index, int start, int end) {
            for (int i = start; i <= end; i++) {
                std::cout << "\tRow " << model->rowCount() << ":\t ";
                for (int col = 0; col < model->columnCount(QModelIndex()); col++) {
                    std::cout << "\t" << model->data(model->index(i, col, index)).toString().toStdString();
                }
                std::cout << std::endl;
            }
        });
        QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [model, &app](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
            if (roles.contains(Akonadi2::Store::ChildrenFetchedRole)) {
                app.quit();
            }
        });
        if (!model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool()) {
            return app.exec();
        }
    } else if (cliOptions.isSet("count")) {
        query.liveQuery = false;
        auto model = loadModel(type, query);
        QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [model, &app](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
            if (roles.contains(Akonadi2::Store::ChildrenFetchedRole)) {
                std::cout << "\tCounted results " << model->rowCount(QModelIndex()) << std::endl;
                app.quit();
            }
        });
        return app.exec();
    } else if (cliOptions.isSet("synchronize")) {
        query.syncOnDemand = true;
        query.processAll = true;
        Akonadi2::Store::synchronize(query).then<void>([&app]() {
            app.quit();
        }).exec();
        app.exec();
    } else {
        query.liveQuery = true;
        if (type == "folder") {
            query.parentProperty = "parent";
        }
        auto model = loadModel(type, query);
        if (type == "folder") {
            QObject::connect(model.data(), &QAbstractItemModel::rowsInserted, [model](const QModelIndex &index, int start, int end) {
                for (int i = start; i <= end; i++) {
                    model->fetchMore(model->index(i, 0, index));
                }
            });
            auto view = QSharedPointer<View<Akonadi2::ApplicationDomain::Folder> >::create(model.data());
            app.exec();
        } else if (type == "mail") {
            auto view = QSharedPointer<View<Akonadi2::ApplicationDomain::Mail> >::create(model.data());
            app.exec();
        } else if (type == "event") {
            auto view = QSharedPointer<View<Akonadi2::ApplicationDomain::Event> >::create(model.data());
            app.exec();
        }
    }
    return 0;
}
