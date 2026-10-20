#include "network/requestmanager.hpp"

#include <QSignalSpy>
#include <QtTest>

using nexusim::client::RequestManager;

class RequestManagerTests : public QObject
{
    Q_OBJECT
private slots:
    void matchesResponseWithoutRequestId()
    {
        RequestManager manager;
        QSignalSpy finished(&manager, &RequestManager::requestFinished);
        const quint64 token = manager.startRequest(1, 2, 1000);
        QVERIFY(token > 0);
        manager.handleMessage(QJsonObject{{"msgid", 2}, {"errno", 0}});
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.takeFirst().at(0).toULongLong(), token);
        QCOMPARE(manager.pendingCount(), 0);
    }

    void matchesStructuredErrorByRequestType()
    {
        RequestManager manager;
        QSignalSpy failed(&manager, &RequestManager::requestFailed);
        const quint64 token = manager.startRequest(4, 5, 1000);
        manager.handleMessage(QJsonObject{{"msgid", 13},
                                          {"request_msgid", 4},
                                          {"error", "FEATURE_DISABLED"},
                                          {"errmsg", "disabled"}});
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.takeFirst().at(0).toULongLong(), token);
    }

    void heartbeatRequiresMatchingCorrelation()
    {
        RequestManager manager;
        QSignalSpy finished(&manager, &RequestManager::requestFinished);
        manager.startRequest(11, 12, 1000, QStringLiteral("expected"));
        manager.handleMessage(QJsonObject{{"msgid", 12},
                                          {"request_id", "other"},
                                          {"ok", true}});
        QCOMPARE(finished.count(), 0);
        manager.handleMessage(QJsonObject{{"msgid", 12},
                                          {"request_id", "expected"},
                                          {"ok", true}});
        QCOMPARE(finished.count(), 1);
    }

    void expiresAndCancelsRequests()
    {
        RequestManager manager;
        QSignalSpy failed(&manager, &RequestManager::requestFailed);
        QSignalSpy finished(&manager, &RequestManager::requestFinished);
        QVERIFY(manager.startRequest(1, 2, 30) > 0);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 500);
        manager.handleMessage(QJsonObject{{"msgid", 2}, {"errno", 0}});
        QCOMPARE(finished.count(), 0);
        QVERIFY(manager.startRequest(4, 5, 1000) > 0);
        manager.cancelAll(QStringLiteral("disconnect"));
        QCOMPARE(failed.count(), 2);
        QCOMPARE(manager.pendingCount(), 0);
        manager.handleMessage(QJsonObject{{"msgid", 5}, {"errno", 0}});
        QCOMPARE(finished.count(), 0);
    }

    void rejectsAmbiguousConcurrentLegacyRequests()
    {
        RequestManager manager;
        QVERIFY(manager.startRequest(1, 2, 1000) > 0);
        QCOMPARE(manager.startRequest(1, 2, 1000), quint64(0));
    }

    void correlatesConcurrentAiRequests()
    {
        RequestManager manager;
        QSignalSpy finished(&manager, &RequestManager::requestFinished);
        const quint64 first = manager.startRequest(14, 15, 1000,
                                                   QStringLiteral("ai-first"));
        const quint64 second = manager.startRequest(14, 15, 1000,
                                                    QStringLiteral("ai-second"));
        QVERIFY(first > 0);
        QVERIFY(second > 0);
        QCOMPARE(manager.startRequest(14, 15, 1000,
                                      QStringLiteral("ai-first")), quint64(0));

        manager.handleMessage(QJsonObject{{"msgid", 15},
                                          {"request_id", "ai-second"},
                                          {"ok", true}});
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.takeFirst().at(0).toULongLong(), second);
        QCOMPARE(manager.pendingCount(), 1);
        manager.handleMessage(QJsonObject{{"msgid", 15},
                                          {"request_id", "ai-first"},
                                          {"ok", true}});
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.takeFirst().at(0).toULongLong(), first);
    }
};

QTEST_GUILESS_MAIN(RequestManagerTests)
#include "test_requestmanager.moc"
