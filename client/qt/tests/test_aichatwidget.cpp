#include "controllers/aicontroller.hpp"
#include "controllers/sessioncontroller.hpp"
#include "network/networkclient.hpp"
#include "network/requestmanager.hpp"
#include "ui/aichatwidget.hpp"

#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QtTest>

using namespace nexusim::client;

class AIChatWidgetTests : public QObject
{
    Q_OBJECT
private slots:
    void rendersControlsAndLongUnicodeReplies()
    {
        NetworkClient network;
        RequestManager requests;
        SessionController session;
        AIController controller(&network, &requests, &session);
        AIChatWidget widget(&controller);
        widget.resize(900, 650);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));

        bool foundTitle = false;
        bool foundNewConversation = false;
        for (QLabel *label : widget.findChildren<QLabel *>())
            foundTitle = foundTitle || label->text().contains(
                QStringLiteral("DeepSeek AI"));
        for (QPushButton *button : widget.findChildren<QPushButton *>())
            foundNewConversation = foundNewConversation ||
                                   button->text() == QStringLiteral("新对话");
        QVERIFY(foundTitle);
        QVERIFY(foundNewConversation);
        QVERIFY(widget.findChild<QPlainTextEdit *>() != nullptr);

        const QString longReply = QStringLiteral("中文长回复 🤖 ") +
                                  QString(3000, QChar(0x6587));
        controller.replyReceived(QStringLiteral("req"), longReply,
                                 QStringLiteral("deepseek-v4-pro"),
                                 QStringLiteral("stop"), 1234, QString());
        QCoreApplication::processEvents();
        bool foundReply = false;
        for (QLabel *label : widget.findChildren<QLabel *>())
            foundReply = foundReply || label->text() == longReply;
        QVERIFY(foundReply);
    }
};

QTEST_MAIN(AIChatWidgetTests)
#include "test_aichatwidget.moc"
