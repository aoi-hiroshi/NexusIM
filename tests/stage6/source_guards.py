#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing {label}: {needle}")


service = read("chatserver/src/server/chatservice.cpp")
friend_model = read("chatserver/src/server/model/friendmoel.cpp")
group_model = read("chatserver/src/server/model/groupmodel.cpp")
offline_model = read("chatserver/src/server/model/offlinemessagemodel.cpp")
wire = read("chatserver/include/server/wireprotocol.hpp")
protocol = read("chatserver/include/public.hpp")
qt_social = read("client/qt/controllers/socialcontroller.cpp")

for message in (
    "ADD_FRIEND_MSG", "QUERY_FRIENDS_MSG", "CREATE_GROUP_MSG",
    "ADD_GROUP_MSG", "QUERY_GROUPS_MSG", "QUERY_GROUP_MEMBERS_MSG",
    "LEAVE_GROUP_MSG", "GROUP_CHAT_MSG",
):
    require(service, f"handlers_.emplace({message}", f"registered {message}")

require(friend_model, "addBidirectional", "symmetric friendship operation")
require(friend_model, "insert ignore into `Friend`", "idempotent friend insert")
require(group_model, "beginTransaction", "transactional group creation")
require(group_model, 'MySQLParameter::text("creator")', "creator membership")
require(group_model, "OwnerCannotLeave", "owner leave rule")
require(service, "senderIsMember", "group sender membership validation")
require(service, 'request["id"] = session->userId', "direct-chat trusted identity")
require(service, '{"id", session->userId}', "group-chat trusted identity")
require(service, "publishToNode", "Redis group routing")
require(service, "offlineMessageModel_.insertBatch(offlineDeliveries)",
        "sender-side group offline storage")
require(service, "CHAT_GROUP_MAX_RECIPIENTS", "bounded group broadcast fanout")
require(service, '{"delivery_guaranteed", false}', "explicit Pub/Sub reliability limit")
require(wire, "queueInLoop", "EventLoop-safe connection send")
require(qt_social, "GroupChatAck", "Qt group request matching")
require(protocol, "GROUP_CHAT_MSG_ACK", "explicit group-chat business ACK")

for model_text, name in (
    (friend_model, "FriendModel"),
    (group_model, "GroupModel"),
    (offline_model, "OfflineMessageModel"),
):
    require(model_text, "MySQLParameter::", f"{name} parameter binding")

if "friendModel_.insert(" in service or "groupModel_.addGroup(" in service:
    raise AssertionError("legacy unaudited social model entrypoint is reachable")

print("stage6_source_guards: PASS")
