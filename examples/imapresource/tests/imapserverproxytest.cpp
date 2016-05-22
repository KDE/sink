#include <QtTest>

#include <QString>
#include <KMime/Message>

#include "../imapserverproxy.h"

#include "log.h"
#include "test.h"

#define ASYNCCOMPARE(actual, expected) \
do {\
    if (!QTest::qCompare(actual, expected, #actual, #expected, __FILE__, __LINE__))\
        return KAsync::error<void>(1, "Comparison failed.");\
} while (0)

#define ASYNCVERIFY(statement) \
do {\
    if (!QTest::qVerify((statement), #statement, "", __FILE__, __LINE__))\
        return KAsync::error<void>(1, "Verify failed.");\
} while (0)

#define VERIFYEXEC(statement) \
do {\
    auto result = statement.exec(); \
    result.waitForFinished(); \
    if (!QTest::qVerify(!result.errorCode(), #statement, "", __FILE__, __LINE__))\
        return;\
} while (0)

#define VERIFYEXEC_FAIL(statement) \
do {\
    auto result = statement.exec(); \
    result.waitForFinished(); \
    if (!QTest::qVerify(result.errorCode(), #statement, "", __FILE__, __LINE__))\
        return;\
} while (0)

/**
 */
class ImapServerProxyTest : public QObject
{
    Q_OBJECT

    QTemporaryDir tempDir;
    QString targetPath;
private slots:
    void initTestCase()
    {
        Sink::Log::setDebugOutputLevel(Sink::Log::Trace);
        system("resetmailbox.sh");
    }

    void cleanup()
    {
    }

    void init()
    {
        qDebug();
        qDebug() << "-----------------------------------------";
        qDebug();
    }

    void testLogin()
    {
        ImapServerProxy imap("localhost", 993);
        VERIFYEXEC(imap.login("doe", "doe"));
    }

    void testLoginFailure()
    {
        ImapServerProxy imap("foobar", 993);
        VERIFYEXEC_FAIL(imap.login("doe", "doe"));
    }

    void testFetchFolders()
    {
        ImapServerProxy imap("localhost", 993);
        auto future = imap.fetchFolders([](const QStringList &){});
        future.waitForFinished();
        QVERIFY(!future.errorCode());
    }

    void testFetchFoldersFailure()
    {
        ImapServerProxy imap("foobar", 993);
        auto future = imap.fetchFolders([](const QStringList &){});
        auto future2 = future;
        future2.waitForFinished();
        QVERIFY(future2.errorCode());
    }

};

QTEST_MAIN(ImapServerProxyTest)
#include "imapserverproxytest.moc"
