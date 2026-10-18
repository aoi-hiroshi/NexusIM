#include "rpcframecodec.h"
#include "simplebuffer.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << std::endl;
        std::exit(1);
    }
}

void testHalfFramesAtEveryBoundary()
{
    const std::string payload(4096, 'r');
    const std::string encoded = mprpc::RpcFrameCodec::encode(payload);
    const mprpc::RpcFrameCodec codec;

    for (std::size_t split = 0; split <= encoded.size(); ++split)
    {
        nexusim::SimpleBuffer buffer;
        std::vector<std::string> frames;
        buffer.append(encoded.data(), split);
        codec.decode(&buffer, [&](std::string frame) {
            frames.push_back(std::move(frame));
        });
        require(frames.empty() || split == encoded.size(),
                "an incomplete RPC frame must not be emitted");

        buffer.append(encoded.data() + split, encoded.size() - split);
        const auto result = codec.decode(&buffer, [&](std::string frame) {
            frames.push_back(std::move(frame));
        });
        require(result.status == mprpc::RpcFrameCodec::Status::Ok,
                "a complete RPC frame must leave no partial bytes");
        require(frames.size() == 1 && frames.front() == payload,
                "a split RPC frame must reconstruct a response larger than 1024 bytes");
    }
}

void testStickyFramesAndPartialTail()
{
    const std::string first = mprpc::RpcFrameCodec::encode("first");
    const std::string second = mprpc::RpcFrameCodec::encode("second");
    const std::string third = mprpc::RpcFrameCodec::encode("third");
    nexusim::SimpleBuffer buffer;
    std::vector<std::string> frames;
    const mprpc::RpcFrameCodec codec;

    buffer.append(first + second + third.substr(0, 6));
    const auto partial = codec.decode(&buffer, [&](std::string frame) {
        frames.push_back(std::move(frame));
    });
    require(partial.status == mprpc::RpcFrameCodec::Status::NeedMoreData,
            "a partial sticky-frame tail must be retained");
    require(frames == std::vector<std::string>({"first", "second"}),
            "all complete sticky RPC frames must be emitted in order");

    buffer.append(third.data() + 6, third.size() - 6);
    codec.decode(&buffer, [&](std::string frame) {
        frames.push_back(std::move(frame));
    });
    require(frames == std::vector<std::string>({"first", "second", "third"}),
            "the retained RPC tail must complete on the next read");
}

void testIllegalLengths()
{
    const mprpc::RpcFrameCodec codec(16);
    nexusim::SimpleBuffer zero;
    const char zeroHeader[4] = {0, 0, 0, 0};
    zero.append(zeroHeader, sizeof(zeroHeader));
    require(codec.decode(&zero, [](std::string) {}).status ==
                mprpc::RpcFrameCodec::Status::InvalidLength,
            "zero-length RPC frames must be rejected");

    nexusim::SimpleBuffer oversized;
    const char oversizedHeader[4] = {0, 0, 0, 17};
    oversized.append(oversizedHeader, sizeof(oversizedHeader));
    const auto result = codec.decode(&oversized, [](std::string) {});
    require(result.status == mprpc::RpcFrameCodec::Status::InvalidLength &&
                result.invalidLength == 17,
            "oversized RPC frames must be rejected before allocation");
}

void testEncodeRejectsInvalidPayloads()
{
    bool emptyRejected = false;
    try
    {
        (void)mprpc::RpcFrameCodec::encode("");
    }
    catch (const std::length_error &)
    {
        emptyRejected = true;
    }
    require(emptyRejected, "empty RPC envelopes must not be encoded");

    bool oversizedRejected = false;
    try
    {
        (void)mprpc::RpcFrameCodec::encode(std::string(17, 'x'), 16);
    }
    catch (const std::length_error &)
    {
        oversizedRejected = true;
    }
    require(oversizedRejected, "oversized outbound RPC envelopes must be rejected");
}

} // namespace

int main()
{
    testHalfFramesAtEveryBoundary();
    testStickyFramesAndPartialTail();
    testIllegalLengths();
    testEncodeRejectsInvalidPayloads();
    std::cout << "stage3_rpc_frame_tests: PASS" << std::endl;
    return 0;
}
