#ifndef NEXUSIM_FRAMECODEC_H
#define NEXUSIM_FRAMECODEC_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace nexusim
{

class FrameCodec
{
public:
    static constexpr std::size_t kHeaderLength = sizeof(std::uint32_t);
    static constexpr std::uint32_t kDefaultMaxFrameLength = 1024U * 1024U;

    enum class Status
    {
        Ok,
        NeedMoreData,
        InvalidLength
    };

    struct DecodeResult
    {
        Status status{Status::Ok};
        std::size_t frameCount{0};
        std::uint32_t invalidLength{0};
    };

    explicit FrameCodec(std::uint32_t maxFrameLength = kDefaultMaxFrameLength)
        : maxFrameLength_(maxFrameLength)
    {
        if (maxFrameLength_ == 0)
        {
            throw std::invalid_argument("max frame length must be positive");
        }
    }

    std::uint32_t maxFrameLength() const noexcept
    {
        return maxFrameLength_;
    }

    static std::string encode(std::string_view payload,
                              std::uint32_t maxFrameLength = kDefaultMaxFrameLength)
    {
        if (payload.empty() || payload.size() > maxFrameLength)
        {
            throw std::length_error("payload length is outside the allowed range");
        }

        const auto length = static_cast<std::uint32_t>(payload.size());
        std::string frame;
        frame.resize(kHeaderLength + payload.size());
        frame[0] = static_cast<char>((length >> 24U) & 0xFFU);
        frame[1] = static_cast<char>((length >> 16U) & 0xFFU);
        frame[2] = static_cast<char>((length >> 8U) & 0xFFU);
        frame[3] = static_cast<char>(length & 0xFFU);
        frame.replace(kHeaderLength, payload.size(), payload.data(), payload.size());
        return frame;
    }

    template <typename Buffer, typename FrameHandler>
    DecodeResult decode(Buffer *buffer, FrameHandler &&handler) const
    {
        DecodeResult result;

        while (buffer->readableBytes() >= kHeaderLength)
        {
            const auto *header = reinterpret_cast<const unsigned char *>(buffer->peek());
            const std::uint32_t payloadLength =
                (static_cast<std::uint32_t>(header[0]) << 24U) |
                (static_cast<std::uint32_t>(header[1]) << 16U) |
                (static_cast<std::uint32_t>(header[2]) << 8U) |
                static_cast<std::uint32_t>(header[3]);

            if (payloadLength == 0 || payloadLength > maxFrameLength_)
            {
                result.status = Status::InvalidLength;
                result.invalidLength = payloadLength;
                return result;
            }

            const std::size_t totalLength = kHeaderLength + payloadLength;
            if (buffer->readableBytes() < totalLength)
            {
                result.status = Status::NeedMoreData;
                return result;
            }

            buffer->retrieve(kHeaderLength);
            auto frame = buffer->retrieveAsString(payloadLength);
            std::forward<FrameHandler>(handler)(std::move(frame));
            ++result.frameCount;
        }

        result.status = buffer->readableBytes() == 0 ? Status::Ok : Status::NeedMoreData;
        return result;
    }

private:
    std::uint32_t maxFrameLength_;
};

} // namespace nexusim

#endif

