#include "protocol/framecodec.hpp"

#include <QtTest>

using nexusim::client::FrameCodec;

class FrameCodecTests : public QObject
{
    Q_OBJECT
private slots:
    void roundTripEverySplit()
    {
        FrameCodec codec;
        const QByteArray payload = QStringLiteral("中文 / English / 😀").toUtf8();
        const QByteArray encoded = codec.encode(payload);
        QVERIFY(!encoded.isEmpty());
        for (int split = 0; split <= encoded.size(); ++split)
        {
            QByteArray buffer = encoded.left(split);
            QList<QByteArray> frames;
            auto first = codec.decode(buffer, frames);
            if (split < encoded.size())
                QCOMPARE(first.frameCount, 0);
            buffer.append(encoded.mid(split));
            auto second = codec.decode(buffer, frames);
            QCOMPARE(second.status, FrameCodec::Status::Ok);
            QCOMPARE(frames.size(), 1);
            QCOMPARE(frames.first(), payload);
            QVERIFY(buffer.isEmpty());
        }
    }

    void stickyAndTrailingHalfFrame()
    {
        FrameCodec codec;
        const QByteArray one = codec.encode("one");
        const QByteArray two = codec.encode("two");
        const QByteArray three = codec.encode("three");
        QByteArray buffer = one + two + three.left(5);
        QList<QByteArray> frames;
        const auto first = codec.decode(buffer, frames);
        QCOMPARE(first.status, FrameCodec::Status::NeedMoreData);
        QCOMPARE(frames, QList<QByteArray>({"one", "two"}));
        buffer.append(three.mid(5));
        const auto second = codec.decode(buffer, frames);
        QCOMPARE(second.status, FrameCodec::Status::Ok);
        QCOMPARE(frames.last(), QByteArray("three"));
    }

    void rejectsIllegalLength()
    {
        FrameCodec codec(8);
        for (quint32 length : {0U, 9U})
        {
            QByteArray buffer(4, '\0');
            buffer[0] = static_cast<char>((length >> 24U) & 0xffU);
            buffer[1] = static_cast<char>((length >> 16U) & 0xffU);
            buffer[2] = static_cast<char>((length >> 8U) & 0xffU);
            buffer[3] = static_cast<char>(length & 0xffU);
            QList<QByteArray> frames;
            const auto result = codec.decode(buffer, frames);
            QCOMPARE(result.status, FrameCodec::Status::InvalidLength);
            QCOMPARE(result.invalidLength, length);
        }
        QString error;
        QVERIFY(codec.encode(QByteArray(), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
};

QTEST_APPLESS_MAIN(FrameCodecTests)
#include "test_framecodec.moc"
