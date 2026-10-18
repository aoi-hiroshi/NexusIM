#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace mprpc
{

// Wire framing shared by MprpcChannel and RpcProvider:
//   uint32 network-byte-order payload length + serialized RpcEnvelope.
class RpcFrameCodec
{
public:
    static constexpr std::size_t kHeaderLength = sizeof(std::uint32_t);
    static constexpr std::uint32_t kDefaultMaxFrameLength = 4U * 1024U * 1024U;

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

    explicit RpcFrameCodec(std::uint32_t maxFrameLength = kDefaultMaxFrameLength)
        : maxFrameLength_(maxFrameLength)
    {
        if (maxFrameLength_ == 0)
        {
            throw std::invalid_argument("max RPC frame length must be positive");
        }
    }

    std::uint32_t maxFrameLength() const noexcept { return maxFrameLength_; }

    static std::string encode(
        std::string_view payload,
        std::uint32_t maxFrameLength = kDefaultMaxFrameLength)
    {
        if (payload.empty() || payload.size() > maxFrameLength)
        {
            throw std::length_error("RPC payload length is outside the allowed range");
        }

        const auto length = static_cast<std::uint32_t>(payload.size());
        std::string frame(kHeaderLength + payload.size(), '\0');
        frame[0] = static_cast<char>((length >> 24U) & 0xffU);
        frame[1] = static_cast<char>((length >> 16U) & 0xffU);
        frame[2] = static_cast<char>((length >> 8U) & 0xffU);
        frame[3] = static_cast<char>(length & 0xffU);
        frame.replace(kHeaderLength, payload.size(), payload.data(), payload.size());
        return frame;
    }

    template <typename Buffer, typename FrameHandler>
    DecodeResult decode(Buffer *buffer, FrameHandler &&handler) const
    {
        DecodeResult result;
        while (buffer->readableBytes() >= kHeaderLength)
        {
            const auto *header =
                reinterpret_cast<const unsigned char *>(buffer->peek());
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
            auto payload = buffer->retrieveAsString(payloadLength);
            std::forward<FrameHandler>(handler)(std::move(payload));
            ++result.frameCount;
        }

        result.status = buffer->readableBytes() == 0 ? Status::Ok
                                                     : Status::NeedMoreData;
        return result;
    }

private:
    std::uint32_t maxFrameLength_;
};

} // namespace mprpc
