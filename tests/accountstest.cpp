#include <QTest>
#include <QDebug>
#include <QSignalSpy>
#include <QAbstractItemModel>
#include <functional>

#include <test.h>
#include <store.h>
#include <log.h>
#include <configstore.h>

class AccountsTest : public QObject
{
    Q_OBJECT
private slots:

    void initTestCase()
    {
        Sink::Test::initTest();
        Sink::Log::setDebugOutputLevel(Sink::Log::Trace);
    }

    void init()
    {
        ConfigStore("accounts").clear();
        ConfigStore("resources").clear();
    }

    void testLoad()
    {
        using namespace Sink;
        using namespace Sink::ApplicationDomain;

        QString accountName("name");
        QString accountIcon("icon");
        auto account = ApplicationDomainType::createEntity<SinkAccount>();
        account.setProperty("type", "maildir");
        account.setProperty("name", accountName);
        account.setProperty("icon", accountIcon);
        Store::create(account).exec().waitForFinished();

        Store::fetchAll<SinkAccount>(Query()).then<void, QList<SinkAccount::Ptr>>([&](const QList<SinkAccount::Ptr> &accounts) {
            QCOMPARE(accounts.size(), 1);
            auto account = accounts.first();
            QCOMPARE(account->getProperty("type").toString(), QString("maildir"));
            QCOMPARE(account->getProperty("name").toString(), accountName);
            QCOMPARE(account->getProperty("icon").toString(), accountIcon);
        })
        .exec().waitForFinished();

        QString smtpServer("smtpServer");
        QString smtpUsername("smtpUsername");
        QString smtpPassword("smtpPassword");
        auto resource = ApplicationDomainType::createEntity<SinkResource>();
        resource.setProperty("type", "org.kde.mailtransport");
        resource.setProperty("account", account.identifier());
        resource.setProperty("server", smtpServer);
        resource.setProperty("username", smtpUsername);
        resource.setProperty("password", smtpPassword);
        Store::create(resource).exec().waitForFinished();


        Store::fetchAll<SinkResource>(Query()).then<void, QList<SinkResource::Ptr>>([&](const QList<SinkResource::Ptr> &resources) {
            QCOMPARE(resources.size(), 1);
            auto resource = resources.first();
            QCOMPARE(resource->getProperty("type").toString(), QString("org.kde.mailtransport"));
            QCOMPARE(resource->getProperty("server").toString(), smtpServer);
        })
        .exec().waitForFinished();

        auto identity = ApplicationDomainType::createEntity<Identity>();
        identity.setProperty("name", smtpServer);
        identity.setProperty("address", smtpUsername);
        identity.setProperty("account", account.identifier());
        Store::create(identity).exec().waitForFinished();

        Store::fetchAll<Identity>(Query()).then<void, QList<Identity::Ptr>>([&](const QList<Identity::Ptr> &identities) {
            QCOMPARE(identities.size(), 1);
        })
        .exec().waitForFinished();


        Store::remove(resource).exec().waitForFinished();

        Store::fetchAll<SinkResource>(Query()).then<void, QList<SinkResource::Ptr>>([](const QList<SinkResource::Ptr> &resources) {
            QCOMPARE(resources.size(), 0);
        })
        .exec().waitForFinished();
    }

    void testLiveQuery()
    {
        using namespace Sink;
        using namespace Sink::ApplicationDomain;

        auto account = ApplicationDomainType::createEntity<SinkAccount>();
        account.setProperty("type", "maildir");
        account.setProperty("name", "name");
        Store::create(account).exec().waitForFinished();

        Query query;
        query.liveQuery = true;
        auto model = Store::loadModel<SinkAccount>(query);
        QTRY_COMPARE(model->rowCount(QModelIndex()), 1);

        auto account2 = ApplicationDomainType::createEntity<SinkAccount>();
        account2.setProperty("type", "maildir");
        account2.setProperty("name", "name");
        Store::create(account2).exec().waitForFinished();
        QTRY_COMPARE(model->rowCount(QModelIndex()), 2);

        //Ensure the notifier only affects one type
        auto resource = ApplicationDomainType::createEntity<SinkResource>();
        resource.setProperty("type", "org.kde.mailtransport");
        Store::create(resource).exec().waitForFinished();
        QTRY_COMPARE(model->rowCount(QModelIndex()), 2);
    }

};

QTEST_GUILESS_MAIN(AccountsTest)
#include "accountstest.moc"
