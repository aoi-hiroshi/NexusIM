#!/usr/bin/env python3
"""Real Stage 6 black-box probe for two ChatServers, MySQL and Redis."""

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "stage2"))
from cluster_probe import Client, login, logout, register  # noqa: E402


ADD_FRIEND = 7
CREATE_GROUP = 8
JOIN_GROUP = 9
GROUP_CHAT = 10
ADD_FRIEND_ACK = 16
QUERY_FRIENDS = 17
QUERY_FRIENDS_ACK = 18
CREATE_GROUP_ACK = 19
JOIN_GROUP_ACK = 20
QUERY_GROUPS = 21
QUERY_GROUPS_ACK = 22
QUERY_MEMBERS = 23
QUERY_MEMBERS_ACK = 24
LEAVE_GROUP = 25
LEAVE_GROUP_ACK = 26
GROUP_CHAT_ACK = 27


def request(client: Client, msgid: int, ack: int, label: str, **fields) -> dict:
    request_id = f"stage6-{label}-{time.time_ns()}"
    client.send({"msgid": msgid, "request_id": request_id, **fields})
    response = client.receive()
    if response.get("msgid") != ack or response.get("request_id") != request_id:
        raise RuntimeError(f"bad {label} response: {response}")
    return response


def expect_ok(response: dict, label: str) -> None:
    if response.get("ok") is not True:
        raise RuntimeError(f"{label} failed: {response}")


def expect_group_delivery(client: Client, sender: int, group_id: int,
                          text: str, expected_name: str) -> dict:
    response = client.receive()
    if response.get("msgid") != GROUP_CHAT or response.get("id") != sender or \
            response.get("groupid") != group_id or response.get("msg") != text:
        raise RuntimeError(f"unexpected group delivery: {response}")
    if response.get("name") != expected_name:
        raise RuntimeError(f"group sender identity was not authoritative: {response}")
    if "session_id" in response:
        raise RuntimeError(f"session leaked in group delivery: {response}")
    return response


def run(args: argparse.Namespace) -> None:
    password = "NexusIM_Stage6_123!"
    suffix = time.time_ns()
    ids = [register(args.host, args.node1_port, f"stage6_{name}_{suffix}",
                    password, args.timeout) for name in ("a", "b", "c", "d")]
    user_a, user_b, user_c, user_d = ids
    a, login_a = login(args.host, args.node1_port, user_a, password, args.timeout)
    b, login_b = login(args.host, args.node2_port, user_b, password, args.timeout)
    c, _ = login(args.host, args.node1_port, user_c, password, args.timeout)
    d, _ = login(args.host, args.node2_port, user_d, password, args.timeout)
    group_id = -1
    try:
        added = request(a, ADD_FRIEND, ADD_FRIEND_ACK, "add-friend", friendid=user_b)
        expect_ok(added, "add friend")
        repeated = request(a, ADD_FRIEND, ADD_FRIEND_ACK, "repeat-friend", friendid=user_b)
        expect_ok(repeated, "repeat add friend")
        if repeated.get("already_friends") is not True:
            raise RuntimeError(f"friend add was not idempotent: {repeated}")
        self_add = request(a, ADD_FRIEND, ADD_FRIEND_ACK, "self-friend", friendid=user_a)
        if self_add.get("error") != "CANNOT_ADD_SELF":
            raise RuntimeError(f"self friendship was not rejected: {self_add}")
        missing = request(a, ADD_FRIEND, ADD_FRIEND_ACK, "missing-friend",
                          friendid=2147483647)
        if missing.get("error") != "USER_NOT_FOUND":
            raise RuntimeError(f"missing friend was not rejected: {missing}")
        for client, expected in ((a, user_b), (b, user_a)):
            friends = request(client, QUERY_FRIENDS, QUERY_FRIENDS_ACK,
                              f"friends-{expected}")
            expect_ok(friends, "query friends")
            if expected not in [item.get("id") for item in friends.get("friends", [])]:
                raise RuntimeError(f"bidirectional friend row missing: {friends}")
        print("friend management and bidirectional idempotency: PASS")

        created = request(a, CREATE_GROUP, CREATE_GROUP_ACK, "create-group",
                          groupname="Stage6 '安全' 群",
                          groupdesc="跨节点群聊验证；引号 ' 作为普通数据")
        expect_ok(created, "create group")
        group_id = created.get("group", {}).get("id", -1)
        if group_id <= 0 or created.get("group", {}).get("role") != "creator":
            raise RuntimeError(f"invalid created group: {created}")
        for client, label in ((b, "join-b"), (c, "join-c")):
            expect_ok(request(client, JOIN_GROUP, JOIN_GROUP_ACK, label,
                              groupid=group_id), label)
        repeated_join = request(b, JOIN_GROUP, JOIN_GROUP_ACK, "repeat-join",
                                groupid=group_id)
        expect_ok(repeated_join, "repeat join")
        if repeated_join.get("already_member") is not True:
            raise RuntimeError(f"group join was not idempotent: {repeated_join}")
        missing_group = request(d, JOIN_GROUP, JOIN_GROUP_ACK, "missing-group",
                                groupid=2147483647)
        if missing_group.get("error") != "GROUP_NOT_FOUND":
            raise RuntimeError(f"missing group was not rejected: {missing_group}")
        groups = request(b, QUERY_GROUPS, QUERY_GROUPS_ACK, "query-groups")
        expect_ok(groups, "query groups")
        if group_id not in [item.get("id") for item in groups.get("groups", [])]:
            raise RuntimeError(f"joined group missing: {groups}")
        members = request(a, QUERY_MEMBERS, QUERY_MEMBERS_ACK, "query-members",
                          groupid=group_id)
        expect_ok(members, "query members")
        if {user_a, user_b, user_c} != {item.get("id") for item in members.get("members", [])}:
            raise RuntimeError(f"wrong group members: {members}")
        unauthorized_members = request(d, QUERY_MEMBERS, QUERY_MEMBERS_ACK,
                                       "unauthorized-members", groupid=group_id)
        if unauthorized_members.get("error") != "NOT_GROUP_MEMBER":
            raise RuntimeError(f"non-member listed members: {unauthorized_members}")
        print("group create/join/list/member authorization: PASS")

        message_a = "A 到 B/C，跨节点与本节点"
        ack_a_id = f"stage6-group-a-{time.time_ns()}"
        a.send({"msgid": GROUP_CHAT, "request_id": ack_a_id,
                "groupid": group_id, "msg": message_a,
                "id": user_d, "name": "forged"})
        expect_group_delivery(b, user_a, group_id, message_a, login_a["name"])
        expect_group_delivery(c, user_a, group_id, message_a, login_a["name"])
        ack_a = a.receive()
        if ack_a.get("msgid") != GROUP_CHAT_ACK or ack_a.get("ok") is not True:
            raise RuntimeError(f"group A ack failed: {ack_a}")

        message_b = "B 回复 A/C"
        ack_b_id = f"stage6-group-b-{time.time_ns()}"
        b.send({"msgid": GROUP_CHAT, "request_id": ack_b_id,
                "groupid": group_id, "msg": message_b})
        expect_group_delivery(a, user_b, group_id, message_b, login_b["name"])
        expect_group_delivery(c, user_b, group_id, message_b, login_b["name"])
        expect_ok(b.receive(), "group B ack")
        denied = request(d, GROUP_CHAT, GROUP_CHAT_ACK, "non-member-chat",
                         groupid=group_id, msg="must not arrive")
        if denied.get("error") != "NOT_GROUP_MEMBER":
            raise RuntimeError(f"non-member group send was not rejected: {denied}")
        print("same-node/cross-node group chat and trusted identity: PASS")

        logout(c)
        c = None
        time.sleep(args.cleanup_wait)
        offline_text = "C 的单条离线群消息"
        ack = request(a, GROUP_CHAT, GROUP_CHAT_ACK, "offline-group",
                      groupid=group_id, msg=offline_text)
        expect_group_delivery(b, user_a, group_id, offline_text, login_a["name"])
        expect_ok(ack, "offline group ack")
        if ack.get("queued_offline", 0) != 1:
            raise RuntimeError(f"offline member was not queued exactly once: {ack}")
        c, c_login = login(args.host, args.node2_port, user_c, password,
                           args.timeout, args.cleanup_wait)
        matching = [json.loads(item) for item in c_login.get("offlinemsg", [])
                    if json.loads(item).get("msg") == offline_text]
        if len(matching) != 1 or matching[0].get("groupid") != group_id:
            raise RuntimeError(f"offline group recovery failed: {c_login}")
        print("offline group message sender-side single storage: PASS")

        owner_leave = request(a, LEAVE_GROUP, LEAVE_GROUP_ACK, "owner-leave",
                              groupid=group_id)
        if owner_leave.get("error") != "GROUP_OWNER_CANNOT_LEAVE":
            raise RuntimeError(f"owner leave rule failed: {owner_leave}")
        expect_ok(request(b, LEAVE_GROUP, LEAVE_GROUP_ACK, "member-leave",
                          groupid=group_id), "member leave")
        after_leave = request(b, GROUP_CHAT, GROUP_CHAT_ACK, "after-leave",
                              groupid=group_id, msg="must not arrive")
        if after_leave.get("error") != "NOT_GROUP_MEMBER":
            raise RuntimeError(f"former member still sent group chat: {after_leave}")
        print("leave-group rules: PASS")
    finally:
        for client in (d, c, b, a):
            if client is not None:
                try:
                    logout(client)
                except OSError:
                    client.close()
    print(f"stage6_social_group_probe: PASS group_id={group_id}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--node1-port", type=int, default=6001)
    parser.add_argument("--node2-port", type=int, default=6002)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--cleanup-wait", type=float, default=1.0)
    run(parser.parse_args())
