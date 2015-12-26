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
#include <QDir>

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

/**
 * A small abstraction layer to use the akonadi store with the type available as string.
 */
class StoreBase {
public:
    virtual Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr getObject() = 0;
    virtual Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr getObject(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier = QByteArray()) = 0;
    virtual KAsync::Job<void> create(const Akonadi2::ApplicationDomain::ApplicationDomainType &type) = 0;
    virtual KAsync::Job<void> modify(const Akonadi2::ApplicationDomain::ApplicationDomainType &type) = 0;
    virtual KAsync::Job<void> remove(const Akonadi2::ApplicationDomain::ApplicationDomainType &type) = 0;
    virtual QSharedPointer<QAbstractItemModel> loadModel(const Akonadi2::Query &query) = 0;
};

template <typename T>
class Store : public StoreBase {
public:
    Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr getObject() Q_DECL_OVERRIDE {
        return T::Ptr::create();
    }

    Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr getObject(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier = QByteArray()) Q_DECL_OVERRIDE {
        return T::Ptr::create(resourceInstanceIdentifier, identifier, 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
    }

    KAsync::Job<void> create(const Akonadi2::ApplicationDomain::ApplicationDomainType &type) Q_DECL_OVERRIDE {
        return Akonadi2::Store::create<T>(*static_cast<const T*>(&type));
    }

    KAsync::Job<void> modify(const Akonadi2::ApplicationDomain::ApplicationDomainType &type) Q_DECL_OVERRIDE {
        return Akonadi2::Store::modify<T>(*static_cast<const T*>(&type));
    }

    KAsync::Job<void> remove(const Akonadi2::ApplicationDomain::ApplicationDomainType &type) Q_DECL_OVERRIDE {
        return Akonadi2::Store::remove<T>(*static_cast<const T*>(&type));
    }

    QSharedPointer<QAbstractItemModel> loadModel(const Akonadi2::Query &query) Q_DECL_OVERRIDE {
        return Akonadi2::Store::loadModel<T>(query);
    }
};

StoreBase& getStore(const QString &type)
{
    if (type == "folder") {
        static Store<Akonadi2::ApplicationDomain::Folder> store;
        return store;
    } else if (type == "mail") {
        static Store<Akonadi2::ApplicationDomain::Mail> store;
        return store;
    } else if (type == "event") {
        static Store<Akonadi2::ApplicationDomain::Event> store;
        return store;
    } else if (type == "resource") {
        static Store<Akonadi2::ApplicationDomain::AkonadiResource> store;
        return store;
    }
    Q_ASSERT(false);
    qWarning() << "Trying to get a store that doesn't exist, falling back to event";
    static Store<Akonadi2::ApplicationDomain::Event> store;
    return store;
}

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
    if (type == "folder") {
        query.requestedProperties << "name" << "parent";
    } else if (type == "mail") {
        query.requestedProperties << "subject" << "folder" << "date";
    } else if (type == "event") {
        query.requestedProperties << "summary";
    } else if (type == "resource") {
        query.requestedProperties << "type";
    }
    auto model = getStore(type).loadModel(query);
    qDebug() << "Folder type " << type;
    qDebug() << "Loaded model in " << time.elapsed() << " ms";
    Q_ASSERT(model);
    return model;
}

QMap<QString, QString> consumeMap(QList<QString> &list)
{
    QMap<QString, QString> map;
    while(list.size() >= 2) {
        map.insert(list.at(0), list.at(1));
        list = list.mid(2);
    }
    return map;
}

int main(int argc, char *argv[])
{
    MyApplication app(argc, argv);

    QCommandLineParser cliOptions;
    cliOptions.addPositionalArgument(QObject::tr("[command]"),
                                     QObject::tr("A command"));
    cliOptions.addPositionalArgument(QObject::tr("[type]"),
                                     QObject::tr("A type to work with"));
    cliOptions.addPositionalArgument(QObject::tr("[resource]"),
                                     QObject::tr("A resource to connect to"));
    cliOptions.addOption(QCommandLineOption("debuglevel", "A debuglevel from 0-6", "debuglevel"));
    cliOptions.addHelpOption();
    cliOptions.process(app);
    QStringList args = cliOptions.positionalArguments();
    auto command = args.takeFirst();

    if (cliOptions.isSet("debuglevel")) {
        Akonadi2::Log::setDebugOutputLevel(static_cast<Akonadi2::Log::DebugLevel>(cliOptions.value("debuglevel").toInt()));
    }

    if (command == "list") {
        auto type = !args.isEmpty() ? args.takeFirst() : QByteArray();
        auto resources = args;

        Akonadi2::Query query;
        for (const auto &res : resources) {
            query.resources << res.toLatin1();
        }
        query.syncOnDemand = false;
        query.processAll = false;
        query.liveQuery = false;

        auto model = loadModel(type, query);
        qDebug() << "Listing";
        int colSize = 38; //Necessary to display a complete UUID
        std::cout << "  Column      ";
        std::cout << QString("Resource").leftJustified(colSize, ' ', true).toStdString();
        std::cout << QString("Identifier").leftJustified(colSize, ' ', true).toStdString();
        for (int i = 0; i < model->columnCount(QModelIndex()); i++) {
            std::cout << " | " << model->headerData(i, Qt::Horizontal).toString().leftJustified(colSize, ' ', true).toStdString();
        }
        std::cout << std::endl;
        QObject::connect(model.data(), &QAbstractItemModel::rowsInserted, [model, colSize](const QModelIndex &index, int start, int end) {
            for (int i = start; i <= end; i++) {
                std::cout << "  Row " << QString::number(model->rowCount()).rightJustified(4, ' ').toStdString() << ": ";
                auto object = model->data(model->index(i, 0, index), Akonadi2::Store::DomainObjectBaseRole).value<Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr>();
                std::cout << "  " << object->resourceInstanceIdentifier().leftJustified(colSize, ' ', true).toStdString();
                std::cout << object->identifier().leftJustified(colSize, ' ', true).toStdString();
                for (int col = 0; col < model->columnCount(QModelIndex()); col++) {
                    std::cout << " | " << model->data(model->index(i, col, index)).toString().leftJustified(colSize, ' ', true).toStdString();
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
    } else if (command == "count") {
        auto type = !args.isEmpty() ? args.takeFirst() : QByteArray();
        auto resources = args;

        Akonadi2::Query query;
        for (const auto &res : resources) {
            query.resources << res.toLatin1();
        }
        query.syncOnDemand = false;
        query.processAll = false;
        query.liveQuery = false;
        auto model = loadModel(type, query);
        QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [model, &app](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
            if (roles.contains(Akonadi2::Store::ChildrenFetchedRole)) {
                std::cout << "\tCounted results " << model->rowCount(QModelIndex()) << std::endl;
                app.quit();
            }
        });
        return app.exec();
    } else if (command == "synchronize") {
        auto resources = args;
        Akonadi2::Query query;
        for (const auto &res : resources) {
            query.resources << res.toLatin1();
        }
        query.syncOnDemand = true;
        query.processAll = true;
        Akonadi2::Store::synchronize(query).then<void>([&app]() {
            app.quit();
        }).exec();
        app.exec();
    } else if (command == "show") {
        auto type = !args.isEmpty() ? args.takeFirst() : QByteArray();
        auto resources = args;

        Akonadi2::Query query;
        for (const auto &res : resources) {
            query.resources << res.toLatin1();
        }
        query.syncOnDemand = false;
        query.processAll = false;
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
    } else if (command == "clear") {
        auto resources = args;

        qDebug() << "Clearing";
        for (const auto &resource : resources) {
            Akonadi2::Store::removeFromDisk(resource.toLatin1());
        }
    } else if (command == "create") {
        auto type = !args.isEmpty() ? args.takeFirst().toLatin1() : QByteArray();
        auto &store = getStore(type);
        Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr object;
        if (type == "resource") {
            auto resourceType = !args.isEmpty() ? args.takeFirst().toLatin1() : QByteArray();
            object = store.getObject("");
            object->setProperty("type", resourceType);
        } else {
            auto resource = !args.isEmpty() ? args.takeFirst().toLatin1() : QByteArray();
            object = store.getObject(resource);
        }
        auto map = consumeMap(args);
        for (auto i = map.begin(); i != map.end(); ++i) {
            object->setProperty(i.key().toLatin1(), i.value());
        }
        auto result = store.create(*object).exec();
        result.waitForFinished();
        if (result.errorCode()) {
            std::cout << "An error occurred while creating the entity: " << result.errorMessage().toStdString();
        }
    } else if (command == "modify") {
        auto type = !args.isEmpty() ? args.takeFirst().toLatin1() : QByteArray();
        auto &store = getStore(type);
        Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr object;
        if (type == "resource") {
            auto identifier = !args.isEmpty() ? args.takeFirst().toLatin1() : QByteArray();
            object = store.getObject("", identifier);
        } else {
            auto resource = !args.isEmpty() ? args.takeFirst().toLatin1() : QByteArray();
            auto identifier = !args.isEmpty() ? args.takeFirst().toLatin1() : QByteArray();
            object = store.getObject(resource, identifier);
        }
        auto map = consumeMap(args);
        for (auto i = map.begin(); i != map.end(); ++i) {
            object->setProperty(i.key().toLatin1(), i.value());
        }
        auto result = store.modify(*object).exec();
        result.waitForFinished();
        if (result.errorCode()) {
            std::cout << "An error occurred while modifying the entity: " << result.errorMessage().toStdString();
        }
    } else if (command == "remove") {
        auto type = !args.isEmpty() ? args.takeFirst().toLatin1() : QByteArray();
        auto &store = getStore(type);
        Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr object;
        if (type == "resource") {
            auto identifier = !args.isEmpty() ? args.takeFirst().toLatin1() : QByteArray();
            object = store.getObject("", identifier);
        } else {
            auto resource = !args.isEmpty() ? args.takeFirst().toLatin1() : QByteArray();
            auto identifier = !args.isEmpty() ? args.takeFirst().toLatin1() : QByteArray();
            object = store.getObject(resource, identifier);
        }
        auto result = store.remove(*object).exec();
        result.waitForFinished();
        if (result.errorCode()) {
            std::cout << "An error occurred while removing the entity: " << result.errorMessage().toStdString();
        }
    } else if (command == "stat") {
        auto resources = args;
        for (const auto &resource : resources) {
            Akonadi2::Storage storage(Akonadi2::storageLocation(), resource, Akonadi2::Storage::ReadOnly);
            auto transaction = storage.createTransaction(Akonadi2::Storage::ReadOnly);

            QList<QByteArray> databases = transaction.getDatabaseNames();
            qint64 total = 0;
            for (const auto &databaseName : databases) {
                std::cout << "Database: " << databaseName.toStdString() << std::endl;
                auto db = transaction.openDatabase(databaseName);
                auto size = db.getSize();
                std::cout << "\tSize [kb]: " << size / 1024 << std::endl;
                total += size;
            }
            std::cout << "Total [kb]: " << total / 1024 << std::endl;
            int diskUsage = 0;
            QDir dir(Akonadi2::storageLocation());
            for (const auto &folder : dir.entryList(QStringList() << resource + "*")) {
                diskUsage += Akonadi2::Storage(Akonadi2::storageLocation(), folder, Akonadi2::Storage::ReadOnly).diskUsage();
            }
            std::cout << "Disk usage [kb]: " << diskUsage / 1024 << std::endl;
        }
    } else {
        qWarning() << "Unknown command " << command;
    }
    return 0;
}
