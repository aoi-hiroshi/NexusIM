#!/usr/bin/env python3
"""Prepare/verify durable friend and group state around a Compose restart."""

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "stage2"))
from cluster_probe import login, logout, register  # noqa: E402

ADD_FRIEND = 7
CREATE_GROUP = 8
JOIN_GROUP = 9
ADD_FRIEND_ACK = 16
QUERY_FRIENDS = 17
QUERY_FRIENDS_ACK = 18
CREATE_GROUP_ACK = 19
JOIN_GROUP_ACK = 20
QUERY_GROUPS = 21
QUERY_GROUPS_ACK = 22
QUERY_MEMBERS = 23
QUERY_MEMBERS_ACK = 24


def request(client, msgid: int, ack: int, label: str, **fields) -> dict:
    request_id = f"stage7-{label}-{time.time_ns()}"
    client.send({"msgid": msgid, "request_id": request_id, **fields})
    response = client.receive()
    if response.get("msgid") != ack or response.get("request_id") != request_id:
        raise RuntimeError(f"unexpected {label} response: {response}")
    if response.get("ok") is not True:
        raise RuntimeError(f"{label} failed: {response}")
    return response


def prepare(args: argparse.Namespace) -> None:
    password = "NexusIM_Stage7_Persistence_123!"
    suffix = time.time_ns()
    user_a = register(args.host, args.port, f"stage7_persist_a_{suffix}",
                      password, args.timeout)
    user_b = register(args.host, args.port, f"stage7_persist_b_{suffix}",
                      password, args.timeout)
    a, _ = login(args.host, args.port, user_a, password, args.timeout)
    b, _ = login(args.host, args.port, user_b, password, args.timeout)
    try:
        request(a, ADD_FRIEND, ADD_FRIEND_ACK, "friend", friendid=user_b)
        created = request(a, CREATE_GROUP, CREATE_GROUP_ACK, "group",
                          groupname=f"Stage7 durable {suffix}",
                          groupdesc="Compose named-volume persistence probe")
        group_id = int(created["group"]["id"])
        request(b, JOIN_GROUP, JOIN_GROUP_ACK, "join", groupid=group_id)
    finally:
        logout(b)
        logout(a)

    state = {
        "user_a": user_a,
        "user_b": user_b,
        "password": password,
        "group_id": group_id,
    }
    Path(args.state_file).write_text(
        json.dumps(state, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"stage7_persistence_prepare: PASS state={args.state_file}")


def verify(args: argparse.Namespace) -> None:
    state = json.loads(Path(args.state_file).read_text(encoding="utf-8"))
    a, _ = login(args.host, args.port, state["user_a"], state["password"],
                 args.timeout)
    b, _ = login(args.host, args.port, state["user_b"], state["password"],
                 args.timeout)
    try:
        friends = request(a, QUERY_FRIENDS, QUERY_FRIENDS_ACK, "friends")
        if state["user_b"] not in [item.get("id") for item in friends["friends"]]:
            raise RuntimeError(f"friend relationship did not persist: {friends}")
        groups = request(b, QUERY_GROUPS, QUERY_GROUPS_ACK, "groups")
        if state["group_id"] not in [item.get("id") for item in groups["groups"]]:
            raise RuntimeError(f"group membership did not persist: {groups}")
        members = request(a, QUERY_MEMBERS, QUERY_MEMBERS_ACK, "members",
                          groupid=state["group_id"])
        expected = {state["user_a"], state["user_b"]}
        actual = {item.get("id") for item in members["members"]}
        if not expected.issubset(actual):
            raise RuntimeError(f"group members did not persist: {members}")
    finally:
        logout(b)
        logout(a)
    print("stage7_persistence_verify: PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("prepare", "verify"))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--state-file", default="/tmp/nexusim-stage7-persistence.json")
    arguments = parser.parse_args()
    prepare(arguments) if arguments.mode == "prepare" else verify(arguments)
