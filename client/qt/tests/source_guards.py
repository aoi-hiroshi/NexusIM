#!/usr/bin/env python3
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing {label}: {needle}")


network = read("client/qt/network/networkclient.cpp")
frame = read("client/qt/protocol/framecodec.hpp") + read(
    "client/qt/protocol/framecodec.cpp"
)
auth = read("client/qt/controllers/authcontroller.cpp")
chat = read("client/qt/controllers/chatcontroller.cpp")
main_window = read("client/qt/ui/mainwindow.cpp")
server_ids = read("chatserver/include/public.hpp")

require(frame, "1024U * 1024U", "one MiB frame limit")
require(frame, "while (buffer.size() >= 4)", "multi-frame decode loop")
require(network, "connectToHost", "asynchronous QTcpSocket connection")
require(network, "readyRead", "asynchronous socket receive")
require(network, "receiveBuffer_.append", "persistent receive buffer")
require(chat, "heartbeatTimer_.setInterval(30000)", "presence heartbeat")
require(auth, "pendingPassword_.clear()", "credential lifetime cleanup")
require(auth, "late\n    // response cannot be mistaken", "legacy auth stale-response isolation")
require(main_window, "群组暂未开放", "disabled server feature disclosure")
require(main_window, "Stage 5", "AI placeholder disclosure")
require(server_ids, "HEARTBEAT_MSG", "server protocol compatibility")

client_sources = "\n".join(
    path.read_text(encoding="utf-8")
    for path in (ROOT / "client/qt").rglob("*.cpp")
)
for forbidden in ("waitForConnected", "waitForReadyRead", "waitForBytesWritten"):
    if forbidden in client_sources:
        raise AssertionError(f"blocking socket API is forbidden: {forbidden}")

if re.search(r"q(?:Debug|Info|Warning|Critical)\s*\([^\n]*password", client_sources,
             re.IGNORECASE):
    raise AssertionError("password must not be written to Qt logs")

for singleton_marker in ("QSharedMemory", "QLocalServer", "QLockFile"):
    if singleton_marker in client_sources:
        raise AssertionError(f"client must remain multi-instance: {singleton_marker}")

print("stage4_qt_source_guards: PASS")
