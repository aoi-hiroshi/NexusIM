#include "rpc_envelope.pb.h"
#include "rpcframecodec.h"
#include "simplebuffer.hpp"
#include "user_service.pb.h"

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

void testLargeProtobufEnvelopeRoundTrip()
{
    nexusim::user::QueryUserResponse serviceResponse;
    serviceResponse.mutable_result()->set_code(nexusim::user::USER_ERROR_OK);
    serviceResponse.mutable_user()->set_user_id(7);
    serviceResponse.mutable_user()->set_username(std::string(4096, 'u'));
    serviceResponse.set_found(true);
    std::string servicePayload;
    require(serviceResponse.SerializeToString(&servicePayload),
            "large service response must serialize");
    require(servicePayload.size() > 1024,
            "test response must exceed the legacy 1024-byte recv limit");

    mprpc::RpcEnvelope outbound;
    outbound.set_protocol_version(1);
    outbound.set_message_type(mprpc::RPC_MESSAGE_TYPE_RESPONSE);
    outbound.set_request_id("large-response");
    outbound.set_status_code(mprpc::RPC_STATUS_OK);
    outbound.set_payload(servicePayload);
    std::string serialized;
    require(outbound.SerializeToString(&serialized), "envelope must serialize");

    nexusim::SimpleBuffer buffer;
    buffer.append(mprpc::RpcFrameCodec::encode(serialized));
    std::vector<std::string> frames;
    mprpc::RpcFrameCodec codec;
    codec.decode(&buffer, [&](std::string frame) {
        frames.push_back(std::move(frame));
    });
    require(frames.size() == 1, "one framed envelope must be decoded");

    mprpc::RpcEnvelope inbound;
    require(inbound.ParseFromString(frames.front()), "envelope must parse");
    nexusim::user::QueryUserResponse parsedResponse;
    require(parsedResponse.ParseFromString(inbound.payload()),
            "service response payload must parse");
    require(parsedResponse.user().username() == std::string(4096, 'u'),
            "large response content must remain intact");
}

void testMalformedProtobufRejected()
{
    mprpc::RpcEnvelope envelope;
    require(!envelope.ParseFromString(std::string("\x0a\xff", 2)),
            "malformed protobuf envelope must be rejected");

    nexusim::user::LoginRequest request;
    const std::string truncatedRequest{
        static_cast<char>(0x12), static_cast<char>(0x08), 'b', 'a', 'd'};
    require(!request.ParseFromString(truncatedRequest),
            "truncated request protobuf must be rejected");
}

} // namespace

int main()
{
    testLargeProtobufEnvelopeRoundTrip();
    testMalformedProtobufRejected();
    std::cout << "stage3_rpc_protocol_tests: PASS" << std::endl;
    return 0;
}
