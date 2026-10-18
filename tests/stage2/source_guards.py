#!/usr/bin/env python3
"""Static regression guards for Stage 2's cross-thread delivery boundary."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REDIS_SOURCE = ROOT / "chatserver/src/server/redis/redis.cpp"
SERVICE_SOURCE = ROOT / "chatserver/src/server/chatservice.cpp"
CHATSERVER_SOURCE = ROOT / "chatserver/src/server/chatserver.cpp"
WIRE_PROTOCOL = ROOT / "chatserver/include/server/wireprotocol.hpp"


def function_body(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


redis_source = REDIS_SOURCE.read_text(encoding="utf-8")
service_source = SERVICE_SOURCE.read_text(encoding="utf-8")
chatserver_source = CHATSERVER_SOURCE.read_text(encoding="utf-8")
wire_protocol = WIRE_PROTOCOL.read_text(encoding="utf-8")

if "subscribeContext_ = connectContext(host, port, false)" not in redis_source:
    raise AssertionError("Pub/Sub connection must not time out while the channel is idle")
if "publishContext_ = connectContext(host, port, true)" not in redis_source or \
        "presenceContext_ = connectContext(host, port, true)" not in redis_source:
    raise AssertionError("synchronous Redis command connections require bounded timeouts")

if "sendFramedAndShutdown" not in wire_protocol or \
        wire_protocol.index("liveConnection->send(frame)") > \
        wire_protocol.index("liveConnection->shutdown()"):
    raise AssertionError("final frame must be sent before EventLoop shutdown")
if "sendFramedAndShutdown(liveConnection, response.dump())" not in chatserver_source:
    raise AssertionError("failed heartbeat must atomically send PRESENCE_LOST before shutdown")

observer = function_body(
    redis_source,
    "void Redis::observerChannelMessages()",
    "std::string Redis::presenceKey",
)
for forbidden in ("TcpConnection", "userConnections_", "sessions_", "offlineMessageModel_"):
    if forbidden in observer:
        raise AssertionError(f"Redis subscription thread touches forbidden state: {forbidden}")
if "handler(*delivery)" not in observer:
    raise AssertionError("subscription thread no longer hands off decoded deliveries")

constructor_start = service_source.index("ChatService::ChatService()")
handler_start = service_source.index("MsgHandler ChatService::getHandler", constructor_start)
constructor = service_source[constructor_start:handler_start]
if "enqueueRedisDelivery" not in constructor:
    raise AssertionError("Redis callback does not enter the bounded handoff")
if 'redisInboundExecutor_("redis-inbound", 1, 512)' not in constructor:
    raise AssertionError("Redis inbound delivery must preserve channel arrival order")

enqueue = function_body(
    service_source,
    "void ChatService::enqueueRedisDelivery",
    "void ChatService::handleRedisDelivery",
)
if "redisInboundExecutor_.trySubmit" not in enqueue:
    raise AssertionError("Redis delivery bypasses the bounded executor")

delivery = function_body(
    service_source,
    "void ChatService::handleRedisDelivery",
    "void ChatService::storeOfflineOrReport",
)
if "localConnectionForUser" not in delivery or "expectedSessionId" not in delivery:
    raise AssertionError("inbound delivery does not recheck local session ownership")
if "offlineMessageModel_" in delivery:
    raise AssertionError("receiving node must not duplicate sender-side offline storage")

print("stage2_source_guards: PASS")
