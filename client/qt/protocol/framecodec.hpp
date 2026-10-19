#ifndef NEXUSIM_QT_FRAMECODEC_HPP
#define NEXUSIM_QT_FRAMECODEC_HPP

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

namespace nexusim {
namespace client {

class FrameCodec
{
public:
    static const quint32 DefaultMaxFrameLength = 1024U * 1024U;

    enum class Status { Ok, NeedMoreData, InvalidLength };
    struct DecodeResult
    {
        Status status{Status::Ok};
        int frameCount{0};
        quint32 invalidLength{0};
    };

    explicit FrameCodec(quint32 maxFrameLength = DefaultMaxFrameLength);

    quint32 maxFrameLength() const;
    QByteArray encode(const QByteArray &payload, QString *error = nullptr) const;
    DecodeResult decode(QByteArray &buffer, QList<QByteArray> &frames) const;

private:
    quint32 maxFrameLength_;
};

} // namespace client
} // namespace nexusim

#endif
