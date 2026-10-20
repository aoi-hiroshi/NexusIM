#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing {label}: {needle}")


deepseek = read("services/ai_service/deepseek_client.cpp")
service = read("services/ai_service/ai_service_impl.cpp")
chat_server = read("chatserver/src/server/chatserver.cpp")
chat_service = read("chatserver/src/server/chatservice.cpp")
qt_protocol = read("client/qt/protocol/messageprotocol.cpp")
qt_ai = read("client/qt/controllers/aicontroller.cpp")
server_ids = read("chatserver/include/public.hpp")

require(deepseek, 'environmentOr("DEEPSEEK_API_KEY", "")',
        "environment-only API key")
require(deepseek, "CURLOPT_SSL_VERIFYPEER, 1L", "TLS peer verification")
require(deepseek, "CURLOPT_SSL_VERIFYHOST, 2L", "TLS host verification")
require(deepseek, "DEEPSEEK_MAX_RESPONSE_BYTES", "bounded response body")
require(deepseek, "localHttpUrl", "loopback-only HTTP mock")
require(service, "AI_MAX_CONCURRENT_REQUESTS", "AIService concurrency limit")
require(service, "conversations_.tryAcquire", "per-conversation serialization")
require(chat_server, 'aiExecutor_("ai-rpc"', "dedicated ChatServer AI executor")
require(chat_server, "if (type == AI_CHAT_MSG)", "AI bypasses connection strand")
require(chat_service, "beginAiRpcRequest", "AI pending request lifecycle")
require(qt_protocol, 'QStringLiteral("prompt")', "Qt AI request")
require(qt_ai, "request_id", "Qt request correlation")
require(server_ids, "AI_CHAT_MSG", "server AI message id")

if "CURLOPT_SSL_VERIFYPEER, 0" in deepseek or "CURLOPT_SSL_VERIFYHOST, 0" in deepseek:
    raise AssertionError("TLS certificate verification must not be disabled")
if re.search(r'(?:cout|clog|LOG_[A-Z]+).*apiKey', deepseek, re.IGNORECASE):
    raise AssertionError("API key must not be logged")
if re.search(r'(?:cout|clog|LOG_[A-Z]+).*prompt', service, re.IGNORECASE):
    raise AssertionError("private prompts must not be logged")
if "api.deepseek.com" in "\n".join(
        path.read_text(encoding="utf-8")
        for path in (ROOT / "client/qt").rglob("*.cpp")):
    raise AssertionError("Qt client must never call the DeepSeek endpoint directly")

print("stage5_source_guards: PASS")
