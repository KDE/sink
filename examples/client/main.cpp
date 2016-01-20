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
#include <QTime>

#include "common/clientapi.h"
#include "common/log.h"

#include <QWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QItemSelectionModel>
#include <iostream>

/**
 * A small abstraction layer to use the sink store with the type available as string.
 */
class StoreBase {
public:
    virtual Sink::ApplicationDomain::ApplicationDomainType::Ptr getObject() = 0;
    virtual Sink::ApplicationDomain::ApplicationDomainType::Ptr getObject(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier = QByteArray()) = 0;
    virtual KAsync::Job<void> create(const Sink::ApplicationDomain::ApplicationDomainType &type) = 0;
    virtual KAsync::Job<void> modify(const Sink::ApplicationDomain::ApplicationDomainType &type) = 0;
    virtual KAsync::Job<void> remove(const Sink::ApplicationDomain::ApplicationDomainType &type) = 0;
    virtual QSharedPointer<QAbstractItemModel> loadModel(const Sink::Query &query) = 0;
};

template <typename T>
class Store : public StoreBase {
public:
    Sink::ApplicationDomain::ApplicationDomainType::Ptr getObject() Q_DECL_OVERRIDE {
        return T::Ptr::create();
    }

    Sink::ApplicationDomain::ApplicationDomainType::Ptr getObject(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier = QByteArray()) Q_DECL_OVERRIDE {
        return T::Ptr::create(resourceInstanceIdentifier, identifier, 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
    }

    KAsync::Job<void> create(const Sink::ApplicationDomain::ApplicationDomainType &type) Q_DECL_OVERRIDE {
        return Sink::Store::create<T>(*static_cast<const T*>(&type));
    }

    KAsync::Job<void> modify(const Sink::ApplicationDomain::ApplicationDomainType &type) Q_DECL_OVERRIDE {
        return Sink::Store::modify<T>(*static_cast<const T*>(&type));
    }

    KAsync::Job<void> remove(const Sink::ApplicationDomain::ApplicationDomainType &type) Q_DECL_OVERRIDE {
        return Sink::Store::remove<T>(*static_cast<const T*>(&type));
    }

    QSharedPointer<QAbstractItemModel> loadModel(const Sink::Query &query) Q_DECL_OVERRIDE {
        return Sink::Store::loadModel<T>(query);
    }
};

StoreBase& getStore(const QString &type)
{
    if (type == "folder") {
        static Store<Sink::ApplicationDomain::Folder> store;
        return store;
    } else if (type == "mail") {
        static Store<Sink::ApplicationDomain::Mail> store;
        return store;
    } else if (type == "event") {
        static Store<Sink::ApplicationDomain::Event> store;
        return store;
    } else if (type == "resource") {
        static Store<Sink::ApplicationDomain::SinkResource> store;
        return store;
    }
    Q_ASSERT(false);
    qWarning() << "Trying to get a store that doesn't exist, falling back to event";
    static Store<Sink::ApplicationDomain::Event> store;
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
            Sink::Query query;
            query.resources << "org.kde.dummy.instance1";
            Sink::Store::synchronize(query).exec();
        });

        auto removeButton = new QPushButton(this);
        removeButton->setText("Remove");
        QObject::connect(removeButton, &QPushButton::pressed, [modelView]() {
            for (auto index : modelView->selectionModel()->selectedIndexes()) {
                auto object = index.data(Sink::Store::DomainObjectRole).value<typename T::Ptr>();
                Sink::Store::remove(*object).exec();
            }
        });

        topLayout->addWidget(titleLabel);
        topLayout->addWidget(syncButton);
        topLayout->addWidget(removeButton);
        topLayout->addWidget(modelView, 10);

        show();
    }

};

static QSharedPointer<QAbstractItemModel> loadModel(const QString &type, Sink::Query query)
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
    QApplication app(argc, argv);

    QCommandLineParser cliOptions;
    cliOptions.addPositionalArgument(QObject::tr("[type]"),
                                     QObject::tr("A type to work with"));
    cliOptions.addPositionalArgument(QObject::tr("[resource]"),
                                     QObject::tr("A resource to connect to"));
    cliOptions.addOption(QCommandLineOption("debuglevel", "A debuglevel from 0-6", "debuglevel"));
    cliOptions.addHelpOption();
    cliOptions.process(app);
    QStringList args = cliOptions.positionalArguments();

    if (cliOptions.isSet("debuglevel")) {
        Sink::Log::setDebugOutputLevel(static_cast<Sink::Log::DebugLevel>(cliOptions.value("debuglevel").toInt()));
    }

    auto type = !args.isEmpty() ? args.takeFirst() : QByteArray();
    auto resources = args;

    Sink::Query query;
    for (const auto &res : resources) {
        query.resources << res.toLatin1();
    }
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
        auto view = QSharedPointer<View<Sink::ApplicationDomain::Folder> >::create(model.data());
        app.exec();
    } else if (type == "mail") {
        auto view = QSharedPointer<View<Sink::ApplicationDomain::Mail> >::create(model.data());
        app.exec();
    } else if (type == "event") {
        auto view = QSharedPointer<View<Sink::ApplicationDomain::Event> >::create(model.data());
        app.exec();
    }
    return 0;
}
