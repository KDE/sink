/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "test.h"

#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include "facade.h"
#include "facadefactory.h"
#include "query.h"
#include "resourceconfig.h"
#include "definitions.h"

using namespace Sink;

void Sink::Test::initTest()
{
    auto logIniFile = Sink::configLocation() + "/log.ini";
    auto areaAutocompletionFile = Sink::dataLocation() + "/debugAreas.ini";

    setTestModeEnabled(true);
    // qDebug() << "Removing " << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).removeRecursively();
    // qDebug() << "Removing " << QStandardPaths::writableLocation(QStandardPaths::DataLocation);
    QDir(QStandardPaths::writableLocation(QStandardPaths::DataLocation)).removeRecursively();
    // qDebug() << "Removing " << QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)).removeRecursively();
    // qDebug() << "Removing " << QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).removeRecursively();
    // qDebug() << "Removing " << QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively();
    // qDebug() << "Removing " << QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    QDir(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)).removeRecursively();
    Log::setPrimaryComponent("test");

    //We copy those files so we can control debug output from outside the test with sinksh
    {
        QDir dir;
        dir.mkpath(Sink::configLocation());

        QFile file(logIniFile);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "Failed to open the file: " << logIniFile;
        } else {
            if (!file.copy(Sink::configLocation() + "/log.ini")) {
                qWarning() << "Failed to move the file: " << Sink::configLocation() + "/log.ini";
            }
        }
    }
    {
        QFile file(areaAutocompletionFile);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "Failed to open the file: " << logIniFile;
        }
        QDir dir;
        dir.mkpath(Sink::dataLocation());
        if (!file.copy(Sink::dataLocation() + "/debugAreas.ini")) {
            qWarning() << "Failed to move the file: " << Sink::configLocation() + "/log.ini";
        }
    }
}

void Sink::Test::setTestModeEnabled(bool enabled)
{
    QStandardPaths::setTestModeEnabled(enabled);
    Sink::clearLocationCache();
    if (enabled) {
        qputenv("SINK_TESTMODE", "TRUE");
    } else {
        qunsetenv("SINK_TESTMODE");
    }
}

bool Sink::Test::testModeEnabled()
{
    return !qEnvironmentVariableIsEmpty("SINK_TESTMODE");
}

template <typename T>
class TestFacade : public Sink::StoreFacade<T>
{
public:
    static std::shared_ptr<TestFacade<T>> registerFacade(Test::TestAccount *testAccount, const QByteArray &instanceIdentifier = QByteArray())
    {
        static QMap<QByteArray, std::shared_ptr<TestFacade<T>>> map;
        auto facade = std::make_shared<TestFacade<T>>();
        facade->mTestAccount = testAccount;
        map.insert(instanceIdentifier, facade);
        bool alwaysReturnFacade = instanceIdentifier.isEmpty();
        Sink::FacadeFactory::instance().registerFacade<T, TestFacade<T>>("testresource", [alwaysReturnFacade](const Sink::ResourceContext &context) {
            if (alwaysReturnFacade) {
                return map.value(QByteArray());
            }
            return map.value(context.resourceInstanceIdentifier);
        });
        return facade;
    }
    ~TestFacade() Q_DECL_OVERRIDE {};
    KAsync::Job<void> create(const T &domainObject) Q_DECL_OVERRIDE
    {
        mTestAccount->addEntity<T>(T::Ptr::create(domainObject));
        return KAsync::null<void>();
    };
    KAsync::Job<void> modify(const T &domainObject) Q_DECL_OVERRIDE
    {
        // mTestAccount->modifyEntity<T>(domainObject);
        return KAsync::null<void>();
    };
    KAsync::Job<void> move(const T &domainObject, const QByteArray &newResource) Q_DECL_OVERRIDE
    {
        // mTestAccount->moveEntity<T>(domainObject, newResource);
        return KAsync::null<void>();
    };
    KAsync::Job<void> copy(const T &domainObject, const QByteArray &newResource) Q_DECL_OVERRIDE
    {
        // mTestAccount->copyEntity<T>(domainObject, newResource);
        return KAsync::null<void>();
    };
    KAsync::Job<void> remove(const T &domainObject) Q_DECL_OVERRIDE
    {
        //FIXME
        // mTestAccount->removeEntity<T>(domainObject);
        return KAsync::null<void>();
    };
    QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename T::Ptr>::Ptr> load(const Sink::Query &query, const Sink::Log::Context &) Q_DECL_OVERRIDE
    {
        auto resultProvider = new Sink::ResultProvider<typename T::Ptr>();
        resultProvider->onDone([resultProvider]() {
            SinkTrace() << "Result provider is done";
            delete resultProvider;
        });
        // We have to do it this way, otherwise we're not setting the fetcher right
        auto emitter = resultProvider->emitter();

        resultProvider->setFetcher([query, resultProvider, this]() {
            SinkTrace() << "Running the fetcher.";
            SinkTrace() << "-------------------------.";
            for (const auto &res : mTestAccount->entities<T>()) {
                resultProvider->add(res.template staticCast<T>());
            }
            resultProvider->initialResultSetComplete(true);
        });
        return qMakePair(KAsync::null(), emitter);
    }

    Test::TestAccount *mTestAccount;
};

Test::TestAccount Sink::Test::TestAccount::registerAccount()
{
    Test::TestAccount account;
    account.mFacades.insert(ApplicationDomain::getTypeName<ApplicationDomain::Folder>(), TestFacade<ApplicationDomain::Folder>::registerFacade(&account));
    account.mFacades.insert(ApplicationDomain::getTypeName<ApplicationDomain::Mail>(), TestFacade<ApplicationDomain::Mail>::registerFacade(&account));
    account.identifier = "testresource.instance1";
    ResourceConfig::addResource(account.identifier, "testresource");
    QMap<QByteArray, QVariant> configuration;
    configuration.insert(ApplicationDomain::SinkResource::Account::name, account.identifier);
    configuration.insert(ApplicationDomain::SinkResource::Capabilities::name, QVariant::fromValue(QByteArrayList() << ApplicationDomain::ResourceCapabilities::Mail::drafts << ApplicationDomain::ResourceCapabilities::Mail::storage << ApplicationDomain::ResourceCapabilities::Mail::transport));
    ResourceConfig::configureResource(account.identifier, configuration);
    return account;
}

template<typename DomainType>
void Sink::Test::TestAccount::addEntity(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)
{
    mEntities[ApplicationDomain::getTypeName<DomainType>()].append(domainObject);
}

template<typename DomainType>
typename DomainType::Ptr Sink::Test::TestAccount::createEntity()
{
    auto entity = DomainType::Ptr::create(ApplicationDomain::ApplicationDomainType::createEntity<DomainType>(identifier));
    addEntity<DomainType>(entity);
    return entity;
}

template<typename DomainType>
QList<Sink::ApplicationDomain::ApplicationDomainType::Ptr> Sink::Test::TestAccount::entities() const
{
    return mEntities.value(ApplicationDomain::getTypeName<DomainType>());
}


#define REGISTER_TYPE(T)                                                          \
    template QList<Sink::ApplicationDomain::ApplicationDomainType::Ptr> Sink::Test::TestAccount::entities<T>() const;           \
    template void Sink::Test::TestAccount::addEntity<T>(const ApplicationDomain::ApplicationDomainType::Ptr &);           \
    template typename T::Ptr Sink::Test::TestAccount::createEntity<T>();

SINK_REGISTER_TYPES()
