#!/usr/bin/env python3
"""Static Stage 3 boundary checks; these do not replace Linux integration tests."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


channel = text("mprpc/src/mprpcchannel.cc")
provider = text("mprpc/src/rpcprovider.cc")
zk = text("mprpc/src/zookeeperutil.cc")
logger = text("mprpc/src/logger.cc")
chat = text("chatserver/src/server/chatservice.cpp")
chat_header = text("chatserver/include/server/chatservice.hpp")
service = text("services/user_service/userserviceimpl.cpp")
proto = text("proto/user_service.proto")
start_user_service = text("deploy/scripts/start-user-service.sh")

assert "RpcFrameCodec::encode" in channel
assert "receiveExact" in channel and "::poll" in channel
assert "DeadlineExceeded" in channel and "deadline_unix_ms" in channel
assert "recv_buf[1024]" not in channel
assert "exit(EXIT_FAILURE)" not in channel

assert "codec_.decode" in provider
assert "executor_.trySubmit" in provider
assert "queueInLoop" in provider
assert "retrieveAllAsString" not in provider
assert "service->CallMethod" in provider
provider_run = provider.split("bool RpcProvider::Run()", 1)[1]
assert "createEphemeralWithRetry" in provider_run
assert "ZNODEEXISTS" in provider
assert provider_run.index("createEphemeralWithRetry") < provider_run.index("server.start();")

assert "wait_for" in zk
assert "exit(EXIT_FAILURE)" not in zk
assert "exit(EXIT_FAILURE)" not in logger

assert "userServiceClient_.login" in chat
assert "userServiceClient_.registerUser" in chat
assert "userServiceClient_.queryUser" in chat
assert "userModel_" not in chat and "UserModel" not in chat_header
assert "bindAuthenticatedOnLoop" in chat
assert "releasePresence" in chat
assert "rpcRequests_.cancelConnection" in chat

assert "PasswordHasher::verify" in service
assert "PasswordHasher::hash" in service
assert "queryById" in service
assert "password" not in proto.split("message PublicUser", 1)[1].split("}", 1)[0]

assert "/dev/tcp/" in start_user_service
assert "UserService exited during startup" in start_user_service
assert "UserService did not listen" in start_user_service

print("stage3_source_guards: PASS")
