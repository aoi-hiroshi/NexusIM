#include "protocol/framecodec.hpp"

#include <algorithm>
#include <stdexcept>

namespace nexusim {
namespace client {

FrameCodec::FrameCodec(quint32 maxFrameLength)
    : maxFrameLength_(maxFrameLength)
{
    if (maxFrameLength_ == 0)
        throw std::invalid_argument("max frame length must be positive");
}

quint32 FrameCodec::maxFrameLength() const
{
    return maxFrameLength_;
}

QByteArray FrameCodec::encode(const QByteArray &payload, QString *error) const
{
    if (payload.isEmpty() || static_cast<quint64>(payload.size()) > maxFrameLength_)
    {
        if (error) *error = QStringLiteral("消息长度必须在 1 字节到 %1 字节之间").arg(maxFrameLength_);
        return QByteArray();
    }

    const quint32 length = static_cast<quint32>(payload.size());
    QByteArray frame;
    frame.resize(4 + payload.size());
    frame[0] = static_cast<char>((length >> 24U) & 0xffU);
    frame[1] = static_cast<char>((length >> 16U) & 0xffU);
    frame[2] = static_cast<char>((length >> 8U) & 0xffU);
    frame[3] = static_cast<char>(length & 0xffU);
    std::copy(payload.constBegin(), payload.constEnd(), frame.begin() + 4);
    if (error) error->clear();
    return frame;
}

FrameCodec::DecodeResult FrameCodec::decode(QByteArray &buffer,
                                             QList<QByteArray> &frames) const
{
    DecodeResult result;
    while (buffer.size() >= 4)
    {
        const auto *header = reinterpret_cast<const unsigned char *>(buffer.constData());
        const quint32 length = (static_cast<quint32>(header[0]) << 24U) |
                               (static_cast<quint32>(header[1]) << 16U) |
                               (static_cast<quint32>(header[2]) << 8U) |
                               static_cast<quint32>(header[3]);
        if (length == 0 || length > maxFrameLength_)
        {
            result.status = Status::InvalidLength;
            result.invalidLength = length;
            return result;
        }
        const quint64 total = 4ULL + length;
        if (static_cast<quint64>(buffer.size()) < total)
        {
            result.status = Status::NeedMoreData;
            return result;
        }
        frames.push_back(buffer.mid(4, static_cast<int>(length)));
        buffer.remove(0, static_cast<int>(total));
        ++result.frameCount;
    }
    result.status = buffer.isEmpty() ? Status::Ok : Status::NeedMoreData;
    return result;
}

} // namespace client
} // namespace nexusim
