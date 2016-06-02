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

using namespace Imap;

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
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.fetchFolders([](const QVector<Folder> &){}));
    }

    void testFetchFoldersFailure()
    {
        ImapServerProxy imap("foobar", 993);
        VERIFYEXEC_FAIL(imap.fetchFolders([](const QVector<Folder> &){}));
    }

    void testFetchMail()
    {
        ImapServerProxy imap("localhost", 993);
        VERIFYEXEC(imap.login("doe", "doe"));

        KIMAP::FetchJob::FetchScope scope;
        scope.mode = KIMAP::FetchJob::FetchScope::Headers;
        int count = 0;
        auto job = imap.select("INBOX.test").then<void>(imap.fetch(KIMAP::ImapSet::fromImapSequenceSet("1:*"), scope,
                    [&count](const QString &mailbox,
                            const QMap<qint64,qint64> &uids,
                            const QMap<qint64,qint64> &sizes,
                            const QMap<qint64,KIMAP::MessageAttribute> &attrs,
                            const QMap<qint64,KIMAP::MessageFlags> &flags,
                            const QMap<qint64,KIMAP::MessagePtr> &messages) {
                        Trace() << "Received " << uids.size() << " messages from " << mailbox;
                        Trace() << uids.size() << sizes.size() << attrs.size() << flags.size() << messages.size();
                        count += uids.size();
                    }));

        VERIFYEXEC(job);
        QCOMPARE(count, 1);
    }

    void testRemoveMail()
    {
        ImapServerProxy imap("localhost", 993);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.remove("INBOX.test", "1:*"));

        KIMAP::FetchJob::FetchScope scope;
        scope.mode = KIMAP::FetchJob::FetchScope::Headers;
        int count = 0;
        auto job = imap.select("INBOX.test").then<void>(imap.fetch(KIMAP::ImapSet::fromImapSequenceSet("1:*"), scope,
                    [&count](const QString &mailbox,
                            const QMap<qint64,qint64> &uids,
                            const QMap<qint64,qint64> &sizes,
                            const QMap<qint64,KIMAP::MessageAttribute> &attrs,
                            const QMap<qint64,KIMAP::MessageFlags> &flags,
                            const QMap<qint64,KIMAP::MessagePtr> &messages) {
                        Trace() << "Received " << uids.size() << " messages from " << mailbox;
                        Trace() << uids.size() << sizes.size() << attrs.size() << flags.size() << messages.size();
                        count += uids.size();
                    }));

        VERIFYEXEC(job);
        QCOMPARE(count, 0);
    }

};

QTEST_MAIN(ImapServerProxyTest)
#include "imapserverproxytest.moc"
