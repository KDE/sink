#include <QtTest>
#include <QDebug>
#include <functional>

#include "store.h"
#include "test.h"
#include "log.h"

using namespace Sink;

/**
 * Test of the test account.
 */
class TestAccountTest : public QObject
{
    Q_OBJECT
private slots:

    void initTestCase()
    {
        // Sink::FacadeFactory::instance().resetFactory();
        // ResourceConfig::clear();
        Log::setDebugOutputLevel(Sink::Log::Trace);
        Test::initTest();
    }

    void testLoad()
    {
        auto &&account = Test::TestAccount::registerAccount();
        auto folder = ApplicationDomain::Folder::Ptr::create(ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Folder>());
        account.addEntity<ApplicationDomain::Folder>(folder);

        auto folders = account.entities<ApplicationDomain::Folder>();
        QCOMPARE(folders.size(), 1);
        QCOMPARE(account.entities<ApplicationDomain::Mail>().size(), 0);

        auto mail = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Mail>();
        Sink::Store::create(ApplicationDomain::Mail(account.identifier)).exec();
        QCOMPARE(account.entities<ApplicationDomain::Mail>().size(), 1);
    }

};

QTEST_MAIN(TestAccountTest)
#include "testaccounttest.moc"
