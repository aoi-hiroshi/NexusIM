#include "framecodec.hpp"
#include "simplebuffer.hpp"
#include "server/concurrency/boundedexecutor.hpp"
#include "server/concurrency/connectionstrand.hpp"
#include "server/concurrency/pendingrequest.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

using namespace std::chrono_literals;

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << std::endl;
        std::exit(1);
    }
}

void testHalfPacketsAtEveryBoundary()
{
    const std::string payload = R"({"msgid":6,"msg":"hello"})";
    const std::string encoded = nexusim::FrameCodec::encode(payload);
    const nexusim::FrameCodec codec;

    for (std::size_t split = 0; split <= encoded.size(); ++split)
    {
        nexusim::SimpleBuffer buffer;
        std::vector<std::string> frames;
        buffer.append(encoded.data(), split);
        const auto first = codec.decode(&buffer, [&](std::string frame) {
            frames.push_back(std::move(frame));
        });
        if (split < encoded.size())
        {
            require(first.frameCount == 0, "partial frame must not be emitted");
        }

        buffer.append(encoded.data() + split, encoded.size() - split);
        const auto second = codec.decode(&buffer, [&](std::string frame) {
            frames.push_back(std::move(frame));
        });
        require(second.status == nexusim::FrameCodec::Status::Ok,
                "complete frame must leave the decoder ready");
        require(frames.size() == 1 && frames.front() == payload,
                "split frame must reconstruct the original payload");
    }
}

void testOneByteAtATime()
{
    const std::string payload(4096, 'x');
    const std::string encoded = nexusim::FrameCodec::encode(payload);
    const nexusim::FrameCodec codec;
    nexusim::SimpleBuffer buffer;
    std::vector<std::string> frames;

    for (char byte : encoded)
    {
        buffer.append(&byte, 1);
        codec.decode(&buffer, [&](std::string frame) { frames.push_back(std::move(frame)); });
    }

    require(frames.size() == 1 && frames.front() == payload,
            "one-byte chunks must reconstruct one frame");
}

void testStickyPacketsAndTail()
{
    const std::string first = nexusim::FrameCodec::encode("one");
    const std::string second = nexusim::FrameCodec::encode("two");
    const std::string third = nexusim::FrameCodec::encode("three");
    nexusim::SimpleBuffer buffer;
    std::vector<std::string> frames;
    const nexusim::FrameCodec codec;

    buffer.append(first + second + third.substr(0, 5));
    const auto partial = codec.decode(&buffer, [&](std::string frame) {
        frames.push_back(std::move(frame));
    });
    require(partial.status == nexusim::FrameCodec::Status::NeedMoreData,
            "partial trailing frame must be retained");
    require(frames == std::vector<std::string>({"one", "two"}),
            "all complete sticky frames must be emitted in order");

    buffer.append(third.data() + 5, third.size() - 5);
    codec.decode(&buffer, [&](std::string frame) { frames.push_back(std::move(frame)); });
    require(frames == std::vector<std::string>({"one", "two", "three"}),
            "trailing partial frame must complete later");
}

void testIllegalLengths()
{
    const nexusim::FrameCodec codec(16);
    nexusim::SimpleBuffer zero;
    const char zeroHeader[4] = {0, 0, 0, 0};
    zero.append(zeroHeader, sizeof(zeroHeader));
    const auto zeroResult = codec.decode(&zero, [](std::string) {});
    require(zeroResult.status == nexusim::FrameCodec::Status::InvalidLength,
            "zero length must be rejected");

    nexusim::SimpleBuffer oversized;
    const char oversizedHeader[4] = {0, 0, 0, 17};
    oversized.append(oversizedHeader, sizeof(oversizedHeader));
    const auto oversizedResult = codec.decode(&oversized, [](std::string) {});
    require(oversizedResult.status == nexusim::FrameCodec::Status::InvalidLength &&
                oversizedResult.invalidLength == 17,
            "oversized length must be rejected before payload allocation");
}

void testConnectionOrderingAndCrossConnectionProgress()
{
    nexusim::ConnectionStrand strand(2, 8, 8);
    std::promise<void> slowStarted;
    std::promise<void> releasePromise;
    const auto releaseSlow = releasePromise.get_future().share();
    std::promise<void> otherFinished;
    std::promise<void> secondFinished;
    std::mutex orderMutex;
    std::vector<int> order;

    require(strand.trySubmit("connection-a", [&] {
                slowStarted.set_value();
                releaseSlow.wait();
                std::lock_guard<std::mutex> lock(orderMutex);
                order.push_back(1);
            }),
            "first connection task must be accepted");
    require(strand.trySubmit("connection-a", [&] {
                std::lock_guard<std::mutex> lock(orderMutex);
                order.push_back(2);
                secondFinished.set_value();
            }),
            "second same-connection task must queue");

    slowStarted.get_future().wait();
    require(strand.trySubmit("connection-b", [&] { otherFinished.set_value(); }),
            "another connection must be accepted");
    require(otherFinished.get_future().wait_for(1s) == std::future_status::ready,
            "slow work on one connection must not block another connection");

    releasePromise.set_value();
    require(secondFinished.get_future().wait_for(1s) == std::future_status::ready,
            "queued work must finish after the preceding task is released");
    strand.shutdown();
    require(order == std::vector<int>({1, 2}),
            "same-connection tasks must execute in receive order");
}

void testHeartbeatExecutorIsIndependent()
{
    nexusim::ConnectionStrand business(1, 4, 4);
    nexusim::BoundedExecutor heartbeat("heartbeat", 1, 4);
    std::promise<void> slowStarted;
    std::promise<void> releaseSlow;
    const auto releaseFuture = releaseSlow.get_future().share();
    std::promise<void> heartbeatFinished;

    require(business.trySubmit("connection-a", [&] {
                slowStarted.set_value();
                releaseFuture.wait();
            }),
            "slow business task must be accepted");
    slowStarted.get_future().wait();
    require(heartbeat.trySubmit([&] { heartbeatFinished.set_value(); }),
            "heartbeat task must use its own bounded executor");
    require(heartbeatFinished.get_future().wait_for(1s) == std::future_status::ready,
            "heartbeat must complete while the connection strand is busy");

    releaseSlow.set_value();
    business.shutdown();
    heartbeat.shutdown();
}

void testPendingRequestLifecycle()
{
    nexusim::PendingRequestRegistry registry;
    const auto now = nexusim::PendingRequestRegistry::Clock::now();
    require(registry.add({"r1", "c1", "s1", now + 1s}),
            "pending request must be registered");
    require(!registry.add({"r1", "c1", "s1", now + 1s}),
            "duplicate request id must be rejected");
    require(registry.complete("r1", "c1", "wrong-session", now) ==
                nexusim::PendingRequestRegistry::Completion::SessionMismatch,
            "a stale session must not complete another session's request");
    require(registry.complete("r1", "c1", "s1", now) ==
                nexusim::PendingRequestRegistry::Completion::Completed,
            "matching request must complete once");

    require(registry.add({"r2", "c1", "s1", now - 1ms}),
            "expired request may be observed before the expiry sweep");
    require(registry.complete("r2", "c1", "s1", now) ==
                nexusim::PendingRequestRegistry::Completion::Expired,
            "late completion must be discarded");

    require(registry.add({"r3", "c1", "s1", now + 1s}), "r3 must register");
    require(registry.add({"r4", "c1", "s2", now + 1s}), "r4 must register");
    require(registry.cancelConnection("c1", "s1") == 1,
            "disconnect must cancel only the matching session");
    require(registry.size() == 1, "new session request must survive old-session cancellation");
}

} // namespace

int main()
{
    testHalfPacketsAtEveryBoundary();
    testOneByteAtATime();
    testStickyPacketsAndTail();
    testIllegalLengths();
    testConnectionOrderingAndCrossConnectionProgress();
    testHeartbeatExecutorIsIndependent();
    testPendingRequestLifecycle();
    std::cout << "stage1_core_tests: PASS" << std::endl;
    return 0;
}

