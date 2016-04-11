#include <QTest>
#include <QDebug>
#include <QSignalSpy>
#include <functional>

#include <test.h>
#include <store.h>
#include <log.h>

class AccountsTest : public QObject
{
    Q_OBJECT
private slots:

    void initTestCase()
    {
        Sink::Test::initTest();
        Sink::Log::setDebugOutputLevel(Sink::Log::Trace);
    }

    void testLoad()
    {
        using namespace Sink;
        using namespace Sink::ApplicationDomain;

        QString accountName("name");
        QString accountIcon("icon");
        auto account = ApplicationDomainType::createEntity<SinkAccount>();
        //FIXME Get rid of this line
        account.setProperty("identifier", account.identifier());
        account.setProperty("type", "maildir");
        account.setProperty("name", accountName);
        account.setProperty("icon", accountIcon);
        Store::create(account).exec().waitForFinished();

        Store::fetchAll<SinkAccount>(Query()).then<void, QList<SinkAccount>>([](const QList<SinkAccount> &accounts) {
            QCOMPARE(accounts.size(), 1);
        })
        .exec().waitForFinished();

        QString smtpServer("smtpServer");
        QString smtpUsername("smtpUsername");
        QString smtpPassword("smtpPassword");
        auto resource = ApplicationDomainType::createEntity<SinkResource>();
        //FIXME Get rid of this line
        resource.setProperty("identifier", resource.identifier());
        resource.setProperty("type", "org.kde.mailtransport");
        resource.setProperty("account", account.identifier());
        resource.setProperty("server", smtpServer);
        resource.setProperty("username", smtpUsername);
        resource.setProperty("password", smtpPassword);
        Store::create(resource).exec().waitForFinished();


        Store::fetchAll<SinkResource>(Query()).then<void, QList<SinkResource>>([](const QList<SinkResource> &resources) {
            QCOMPARE(resources.size(), 1);
        })
        .exec().waitForFinished();

        Store::remove(resource).exec().waitForFinished();

        Store::fetchAll<SinkResource>(Query()).then<void, QList<SinkResource>>([](const QList<SinkResource> &resources) {
            QCOMPARE(resources.size(), 0);
        })
        .exec().waitForFinished();
    }
};

QTEST_GUILESS_MAIN(AccountsTest)
#include "accountstest.moc"
