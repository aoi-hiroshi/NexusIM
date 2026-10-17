#!/usr/bin/env python3
"""Black-box checks for session identity, heartbeat, and the V1 allowlist."""

import argparse
import socket
import time

from auth_probe import (
    LOGIN_MSG,
    LOGIN_MSG_ACK,
    REG_MSG,
    REG_MSG_ACK,
    display_response,
    receive_json,
    send_json,
)


ONE_CHAT_MSG = 6
ADD_FRIEND_MSG = 7
HEARTBEAT_MSG = 11
HEARTBEAT_MSG_ACK = 12
ERROR_MSG = 13


def register_and_login(connection: socket.socket, name: str, password: str) -> int:
    send_json(connection, {"msgid": REG_MSG, "name": name, "password": password})
    registration = receive_json(connection)
    if registration.get("msgid") != REG_MSG_ACK or registration.get("errno") != 0:
        raise RuntimeError(f"registration failed for {name}: {registration}")
    user_id = registration.get("id")
    if not isinstance(user_id, int) or user_id <= 0:
        raise RuntimeError(f"registration returned an invalid id: {registration}")

    send_json(connection, {"msgid": LOGIN_MSG, "id": user_id, "password": password})
    login = receive_json(connection)
    if login.get("msgid") != LOGIN_MSG_ACK or login.get("errno") != 0:
        raise RuntimeError(f"login failed for {name}: {login}")
    if login.get("id") != user_id or login.get("name") != name:
        raise RuntimeError(f"login returned the wrong identity: {login}")
    if len(login.get("session_id", "")) != 64:
        raise RuntimeError(f"login returned an invalid session id: {login}")
    return user_id


def expect_error(connection: socket.socket, request: dict, code: str) -> None:
    send_json(connection, request)
    response = receive_json(connection)
    if response.get("msgid") != ERROR_MSG or response.get("error") != code:
        raise RuntimeError(f"expected {code}, received {response}")


def run(host: str, port: int, timeout: float) -> None:
    password = "NexusIM_Test_123!"
    suffix = str(time.time_ns())
    sender_name = f"security_sender_{suffix}"
    receiver_name = f"security_receiver_{suffix}"

    # Heartbeats cannot authenticate a connection, and disabled legacy handlers
    # remain unreachable even when a client knows their numeric message IDs.
    with socket.create_connection((host, port), timeout=timeout) as unauthenticated:
        unauthenticated.settimeout(timeout)
        expect_error(unauthenticated, {
            "msgid": HEARTBEAT_MSG,
            "request_id": "unauthenticated-heartbeat",
        }, "UNAUTHENTICATED")
        expect_error(unauthenticated, {
            "msgid": ADD_FRIEND_MSG,
            "friendid": 1,
        }, "FEATURE_DISABLED")

    with socket.create_connection((host, port), timeout=timeout) as receiver, \
            socket.create_connection((host, port), timeout=timeout) as sender:
        receiver.settimeout(timeout)
        sender.settimeout(timeout)
        receiver_id = register_and_login(receiver, receiver_name, password)
        sender_id = register_and_login(sender, sender_name, password)

        request_id = f"heartbeat-{suffix}"
        send_json(sender, {"msgid": HEARTBEAT_MSG, "request_id": request_id})
        heartbeat = receive_json(sender)
        display_response("HEARTBEAT", heartbeat)
        if heartbeat.get("msgid") != HEARTBEAT_MSG_ACK or \
                heartbeat.get("request_id") != request_id or heartbeat.get("ok") is not True:
            raise RuntimeError(f"authenticated heartbeat failed: {heartbeat}")

        send_json(sender, {
            "msgid": ONE_CHAT_MSG,
            "toid": receiver_id,
            "msg": "identity overwrite probe",
            "id": 999999,
            "name": "forged-client-name",
            "session_id": "forged-client-session",
        })
        delivered = receive_json(receiver)
        display_response("DELIVERED", delivered)
        if delivered.get("msgid") != ONE_CHAT_MSG:
            raise RuntimeError(f"receiver got an unexpected message: {delivered}")
        if delivered.get("id") != sender_id or delivered.get("name") != sender_name:
            raise RuntimeError(f"client forged the delivered identity: {delivered}")
        if "session_id" in delivered:
            raise RuntimeError(f"session id leaked into the delivered message: {delivered}")

    print(
        "session_security_probe: PASS "
        f"(sender_id={sender_id}, receiver_id={receiver_id})"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6000)
    parser.add_argument("--timeout", type=float, default=5.0)
    arguments = parser.parse_args()
    run(arguments.host, arguments.port, arguments.timeout)
