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
#include <QStringListModel>

#include "common/clientapi.h"
#include "common/resultprovider.h"
#include "common/resource.h"
#include "common/synclistresult.h"
#include "console.h"

#include <QWidget>
#include <QListView>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

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
            Akonadi2::Store::synchronize("org.kde.dummy");
        });

        topLayout->addWidget(titleLabel);
        topLayout->addWidget(syncButton);
        topLayout->addWidget(listView, 10);

        show();
    }

};

template<class T>
class AkonadiListModel : public QStringListModel
{
public:
    AkonadiListModel(const QSharedPointer<async::ResultEmitter<T> > &emitter, const QByteArray &property)
        :QStringListModel(),
        mEmitter(emitter)
    {
        emitter->onAdded([this, property](const T &value) {
            // qDebug() << "VALUE ADDED";
            Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr type(value);
            mStringList << type->getProperty(property).toString();
            setStringList(mStringList);
        });
        emitter->onInitialResultSetComplete([this]() {
        });
        emitter->onComplete([this]() {
            // qDebug() << "COMPLETE";
            mEmitter.clear();
        });
        emitter->onClear([this]() {
            // qDebug() << "CLEAR";
            mStringList.clear();
            setStringList(mStringList);
        });
    }

private:
    QSharedPointer<async::ResultEmitter<T> > mEmitter;
    QStringList mStringList;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Akonadi2::Storage store(Akonadi2::Store::storageLocation(), "org.kde.dummy", Akonadi2::Storage::ReadWrite);
    store.removeFromDisk();

    QCommandLineParser cliOptions;
    cliOptions.addPositionalArgument(QObject::tr("[resource]"),
                                     QObject::tr("A resource to connect to"));
    cliOptions.process(app);
    QStringList resources = cliOptions.positionalArguments();
    if (resources.isEmpty()) {
        resources << "org.kde.dummy";
    }

    //FIXME move to clientapi
    Akonadi2::ResourceFactory::load("org.kde.dummy");

    Akonadi2::Query query;
    query.resources << "org.kde.dummy";
    query.syncOnDemand = false;
    query.processAll = false;
    query.liveQuery = true;
    // query.propertyFilter.insert("uid", "testuid");

    auto model = QSharedPointer<AkonadiListModel<Akonadi2::ApplicationDomain::Event::Ptr> >::create(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query), "summary");
    auto view = QSharedPointer<View>::create(model.data());

    return app.exec();
}
