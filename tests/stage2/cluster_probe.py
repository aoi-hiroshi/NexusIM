#!/usr/bin/env python3
"""Black-box Stage 2 probe for local, cross-node, duplicate and Nginx routes.

This script creates disposable users in the configured NexusIM test database.
It requires two real ChatServer processes, Redis and MySQL. Nginx is optional.
"""

import argparse
import json
import socket
import struct
import time


LOGIN_MSG = 1
LOGIN_MSG_ACK = 2
LOGINOUT_MSG = 3
REG_MSG = 4
REG_MSG_ACK = 5
ONE_CHAT_MSG = 6
HEARTBEAT_MSG = 11
HEARTBEAT_MSG_ACK = 12
ERROR_MSG = 13
MAX_FRAME_LENGTH = 1024 * 1024


class Client:
    def __init__(self, host: str, port: int, timeout: float):
        self.socket = socket.create_connection((host, port), timeout=timeout)
        self.socket.settimeout(timeout)

    def close(self) -> None:
        try:
            self.socket.close()
        except OSError:
            pass

    def send(self, value: dict) -> None:
        payload = json.dumps(value, separators=(",", ":")).encode("utf-8")
        if not 0 < len(payload) <= MAX_FRAME_LENGTH:
            raise ValueError("invalid frame length")
        self.socket.sendall(struct.pack("!I", len(payload)) + payload)

    def receive(self) -> dict:
        header = self._receive_exact(4)
        length = struct.unpack("!I", header)[0]
        if not 0 < length <= MAX_FRAME_LENGTH:
            raise RuntimeError(f"server returned invalid frame length {length}")
        return json.loads(self._receive_exact(length).decode("utf-8"))

    def _receive_exact(self, size: int) -> bytes:
        result = bytearray()
        while len(result) < size:
            chunk = self.socket.recv(size - len(result))
            if not chunk:
                raise RuntimeError("server closed before returning a complete frame")
            result.extend(chunk)
        return bytes(result)


def register(host: str, port: int, name: str, password: str,
             timeout: float) -> int:
    client = Client(host, port, timeout)
    try:
        client.send({"msgid": REG_MSG, "name": name, "password": password})
        response = client.receive()
        if response.get("msgid") != REG_MSG_ACK or response.get("errno") != 0:
            raise RuntimeError(f"registration failed: {response}")
        user_id = response.get("id")
        if not isinstance(user_id, int) or user_id <= 0:
            raise RuntimeError(f"registration returned invalid id: {response}")
        return user_id
    finally:
        client.close()


def login(host: str, port: int, user_id: int, password: str,
          timeout: float, retry_seconds: float = 0.0):
    deadline = time.monotonic() + retry_seconds
    while True:
        client = Client(host, port, timeout)
        client.send({"msgid": LOGIN_MSG, "id": user_id, "password": password})
        response = client.receive()
        if response.get("msgid") == LOGIN_MSG_ACK and response.get("errno") == 0:
            if len(response.get("session_id", "")) != 64:
                client.close()
                raise RuntimeError(f"login returned invalid session: {response}")
            return client, response
        client.close()
        if response.get("errno") != 2 or time.monotonic() >= deadline:
            raise RuntimeError(f"login failed: {response}")
        time.sleep(0.1)


def logout(client: Client) -> None:
    try:
        client.send({"msgid": LOGINOUT_MSG})
    finally:
        client.close()


def assert_chat(response: dict, sender_id: int, recipient_id: int,
                message: str) -> None:
    if response.get("msgid") != ONE_CHAT_MSG or response.get("id") != sender_id or \
            response.get("toid") != recipient_id or response.get("msg") != message:
        raise RuntimeError(f"unexpected chat delivery: {response}")


def fence_business_queue(client: Client) -> None:
    client.send({"msgid": 999999})
    response = client.receive()
    if response.get("msgid") != ERROR_MSG or response.get("error") != "FEATURE_DISABLED":
        raise RuntimeError(f"business queue fence failed: {response}")


def heartbeat(client: Client, label: str) -> None:
    request_id = f"stage2-{label}-{time.time_ns()}"
    client.send({"msgid": HEARTBEAT_MSG, "request_id": request_id})
    response = client.receive()
    if response.get("msgid") != HEARTBEAT_MSG_ACK or \
            response.get("request_id") != request_id or response.get("ok") is not True:
        raise RuntimeError(f"heartbeat failed: {response}")


def run(args: argparse.Namespace) -> None:
    password = "NexusIM_Stage2_123!"
    suffix = time.time_ns()
    user_a = register(args.host, args.node1_port, f"stage2_a_{suffix}",
                      password, args.timeout)
    user_b = register(args.host, args.node1_port, f"stage2_b_{suffix}",
                      password, args.timeout)
    user_c = register(args.host, args.node1_port, f"stage2_c_{suffix}",
                      password, args.timeout)
    print(f"created test users A={user_a}, B={user_b}, offline C={user_c}")

    # Test 1: both sessions are pinned to node 1 and bypass Redis Pub/Sub.
    local_a, login_a = login(args.host, args.node1_port, user_a, password, args.timeout)
    local_b, _ = login(args.host, args.node1_port, user_b, password, args.timeout)
    try:
        local_a.send({"msgid": ONE_CHAT_MSG, "id": user_b,
                      "name": "forged", "toid": user_b, "msg": "local-route"})
        delivered = local_b.receive()
        assert_chat(delivered, user_a, user_b, "local-route")
        if delivered.get("name") != login_a.get("name"):
            raise RuntimeError("server did not replace forged sender name")
    finally:
        logout(local_b)
        logout(local_a)
    print("test 1 local direct chat: PASS")

    # Tests 2 and 3: deterministic direct ports establish a cross-node route.
    cross_a, _ = login(args.host, args.node1_port, user_a, password,
                       args.timeout, args.cleanup_wait)
    cross_b, _ = login(args.host, args.node2_port, user_b, password,
                       args.timeout, args.cleanup_wait)
    duplicate = Client(args.host, args.node2_port, args.timeout)
    try:
        duplicate.send({"msgid": LOGIN_MSG, "id": user_a, "password": password})
        duplicate_response = duplicate.receive()
        if duplicate_response.get("msgid") != LOGIN_MSG_ACK or \
                duplicate_response.get("errno") != 2:
            raise RuntimeError(f"duplicate login was not rejected: {duplicate_response}")
    finally:
        duplicate.close()
    print("test 3 cross-node duplicate login: PASS")

    cross_a.send({"msgid": ONE_CHAT_MSG, "toid": user_b,
                  "id": -99, "name": "forged", "msg": "node1-to-node2"})
    assert_chat(cross_b.receive(), user_a, user_b, "node1-to-node2")
    cross_b.send({"msgid": ONE_CHAT_MSG, "toid": user_a,
                  "id": -99, "name": "forged", "msg": "node2-to-node1"})
    assert_chat(cross_a.receive(), user_b, user_a, "node2-to-node1")
    heartbeat(cross_a, "a")
    heartbeat(cross_b, "b")
    print("test 2 bidirectional Redis cross-node chat: PASS")
    print("test 4 authenticated Presence renewal: PASS")

    cross_a.send({"msgid": ONE_CHAT_MSG, "toid": 2147483647,
                  "msg": "invalid-recipient"})
    invalid_recipient = cross_a.receive()
    if invalid_recipient.get("msgid") != ERROR_MSG or \
            invalid_recipient.get("error") != "RECIPIENT_NOT_FOUND" or \
            invalid_recipient.get("queued_offline") is not False:
        raise RuntimeError(f"invalid recipient was not rejected: {invalid_recipient}")
    print("recipient existence validation: PASS")

    # C has never logged in, so no disconnect-cleanup timing is involved. The
    # sender node is the only node that stores its offline message. A same-strand
    # disabled request acts as a fence before C logs in and reads the row.
    cross_a.send({"msgid": ONE_CHAT_MSG, "toid": user_c, "msg": "offline-once"})
    fence_business_queue(cross_a)
    logout(cross_b)
    logout(cross_a)
    offline_c, offline_login = login(args.host, args.node2_port, user_c,
                                     password, args.timeout, args.cleanup_wait)
    try:
        offline_messages = offline_login.get("offlinemsg", [])
        matching = [json.loads(item) for item in offline_messages
                    if json.loads(item).get("msg") == "offline-once"]
        if len(matching) != 1:
            raise RuntimeError(
                f"expected one offline message, got {len(matching)}: {offline_messages}"
            )
    finally:
        logout(offline_c)
    print("offline sender-side single storage: PASS")

    if args.skip_nginx:
        print("test 5 Nginx distribution: SKIPPED by --skip-nginx")
        return

    nginx_clients = []
    nodes = {}
    try:
        for index in range(args.nginx_connections):
            # Registration uses a deterministic backend so only the measured
            # long-lived login connections advance Nginx's round-robin state.
            user_id = register(args.host, args.node1_port,
                               f"stage2_nginx_{suffix}_{index}", password,
                               args.timeout)
            client, response = login(args.host, args.nginx_port, user_id,
                                     password, args.timeout)
            nginx_clients.append(client)
            node_id = response.get("node_id")
            nodes[node_id] = nodes.get(node_id, 0) + 1
        if len(nodes) != 2:
            raise RuntimeError(f"Nginx connections did not reach both nodes: {nodes}")
    finally:
        for client in nginx_clients:
            logout(client)
    print(f"test 5 Nginx stream distribution: PASS {nodes}")
    print("cluster_probe: PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--node1-port", type=int, default=6001)
    parser.add_argument("--node2-port", type=int, default=6002)
    parser.add_argument("--nginx-port", type=int, default=8000)
    parser.add_argument("--nginx-connections", type=int, default=6)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--cleanup-wait", type=float, default=5.0)
    parser.add_argument("--skip-nginx", action="store_true")
    run(parser.parse_args())
