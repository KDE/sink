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
#include <QAbstractListModel>

#include "common/clientapi.h"
#include "common/resultprovider.h"
#include "common/resource.h"
#include "common/synclistresult.h"
#include "common/storage.h"
#include "common/domain/event.h"
#include "console.h"

#include <QWidget>
#include <QListView>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QItemSelectionModel>

enum Roles {
    DomainObjectRole = Qt::UserRole + 1
};

template <typename T>
class View : public QWidget
{
public:
    View(QAbstractItemModel *model)
        : QWidget()
    {
        auto listView = new QListView(this);
        listView->setModel(model);
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
            Akonadi2::Store::synchronize("org.kde.dummy.instance1");
        });

        auto removeButton = new QPushButton(this);
        removeButton->setText("Remove");
        QObject::connect(removeButton, &QPushButton::pressed, [listView]() {
            for (auto index :listView->selectionModel()->selectedIndexes()) {
                auto object = index.data(DomainObjectRole).value<typename T::Ptr>();
                Akonadi2::Store::remove(*object, "org.kde.dummy.instance1").exec();
            }
        });

        topLayout->addWidget(titleLabel);
        topLayout->addWidget(syncButton);
        topLayout->addWidget(listView, 10);

        show();
    }

};

template<class T>
class AkonadiListModel : public QAbstractListModel
{
public:
    AkonadiListModel(const QSharedPointer<Akonadi2::ResultEmitter<T> > &emitter, const QByteArray &property)
        :QAbstractListModel(),
        mEmitter(emitter),
        mProperty(property)
    {
        emitter->onAdded([this, property](const T &value) {
            const auto keys = mEntities.keys();
            int index = 0;
            for (; index < keys.size(); index++) {
                if (value->identifier() < keys.at(index)) {
                    break;
                }
            }
            beginInsertRows(QModelIndex(), index, index);
            mEntities.insert(value->identifier(), value);
            endInsertRows();
        });
        emitter->onModified([this, property](const T &value) {
            mEntities.remove(value->identifier());
            mEntities.insert(value->identifier(), value);
            //FIXME
            // emit dataChanged();
        });
        emitter->onRemoved([this, property](const T &value) {
            auto index = mEntities.keys().indexOf(value->identifier());
            beginRemoveRows(QModelIndex(), index, index);
            mEntities.remove(value->identifier());
            endRemoveRows();
        });
        emitter->onInitialResultSetComplete([this]() {
        });
        emitter->onComplete([this]() {
            // qDebug() << "COMPLETE";
            mEmitter.clear();
        });
        emitter->onClear([this]() {
            // qDebug() << "CLEAR";
            beginResetModel();
            mEntities.clear();
            endResetModel();
        });
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const
    {
        return mEntities.size();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const
    {
        if (index.row() >= mEntities.size()) {
            qWarning() << "Out of bounds access";
            return QVariant();
        }
        if (role == Qt::DisplayRole) {
            auto entity = mEntities.value(mEntities.keys().at(index.row()));
            return entity->getProperty(mProperty).toString() + entity->identifier();
        }
        if (role == DomainObjectRole) {
            return QVariant::fromValue(mEntities.value(mEntities.keys().at(index.row())));
        }
        return QVariant();
    }

private:
    QSharedPointer<Akonadi2::ResultEmitter<T> > mEmitter;
    QMap<QByteArray, T> mEntities;
    QByteArray mProperty;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCommandLineParser cliOptions;
    cliOptions.addPositionalArgument(QObject::tr("[resource]"),
                                     QObject::tr("A resource to connect to"));
    cliOptions.addOption(QCommandLineOption("clear"));
    cliOptions.addOption(QCommandLineOption("debuglevel"));
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
    query.liveQuery = true;

    auto model = QSharedPointer<AkonadiListModel<Akonadi2::ApplicationDomain::Event::Ptr> >::create(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query), "summary");
    auto view = QSharedPointer<View<Akonadi2::ApplicationDomain::Event> >::create(model.data());

    return app.exec();
}
