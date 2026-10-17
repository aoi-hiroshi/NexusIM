#!/usr/bin/env python3
"""Register and login through the NexusIM framed TCP protocol."""

import argparse
import json
import socket
import struct
import time


# Keep these values synchronized with chatserver/include/public.hpp.
LOGIN_MSG = 1
LOGIN_MSG_ACK = 2
REG_MSG = 4
REG_MSG_ACK = 5
MAX_FRAME_LENGTH = 1024 * 1024


def receive_exact(connection: socket.socket, size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        chunk = connection.recv(size - len(result))
        if not chunk:
            raise RuntimeError("server closed the connection before a complete response")
        result.extend(chunk)
    return bytes(result)


def send_json(connection: socket.socket, value: dict) -> None:
    payload = json.dumps(value, separators=(",", ":")).encode("utf-8")
    if not 0 < len(payload) <= MAX_FRAME_LENGTH:
        raise ValueError("request payload has an invalid length")
    connection.sendall(struct.pack("!I", len(payload)) + payload)


def receive_json(connection: socket.socket) -> dict:
    length = struct.unpack("!I", receive_exact(connection, 4))[0]
    if not 0 < length <= MAX_FRAME_LENGTH:
        raise RuntimeError(f"server returned invalid frame length {length}")
    return json.loads(receive_exact(connection, length).decode("utf-8"))


def display_response(label: str, response: dict) -> None:
    printable = dict(response)
    session_id = printable.get("session_id")
    if isinstance(session_id, str):
        printable["session_id"] = f"{session_id[:8]}... ({len(session_id)} chars)"
    print(f"{label}: {json.dumps(printable, ensure_ascii=False, sort_keys=True)}")


def run(host: str, port: int, name: str, password: str, timeout: float) -> None:
    if not 1 <= len(name.encode("utf-8")) <= 50:
        raise ValueError("name must contain 1-50 UTF-8 bytes")
    if not 8 <= len(password.encode("utf-8")) <= 128:
        raise ValueError("password must contain 8-128 UTF-8 bytes")

    with socket.create_connection((host, port), timeout=timeout) as connection:
        connection.settimeout(timeout)

        send_json(connection, {
            "msgid": REG_MSG,
            "name": name,
            "password": password,
        })
        registration = receive_json(connection)
        display_response("REGISTER", registration)
        if registration.get("msgid") != REG_MSG_ACK or registration.get("errno") != 0:
            raise RuntimeError(f"registration failed: {registration}")

        user_id = registration.get("id")
        if not isinstance(user_id, int) or user_id <= 0:
            raise RuntimeError(f"registration returned an invalid user id: {registration}")

        send_json(connection, {
            "msgid": LOGIN_MSG,
            "id": user_id,
            "password": password,
        })
        login = receive_json(connection)
        display_response("LOGIN", login)
        if login.get("msgid") != LOGIN_MSG_ACK or login.get("errno") != 0:
            raise RuntimeError(f"login failed: {login}")
        if login.get("id") != user_id:
            raise RuntimeError(f"login returned the wrong user id: {login}")
        if len(login.get("session_id", "")) != 64:
            raise RuntimeError(f"login returned an invalid session id: {login}")

    print(f"auth_probe: PASS (user_id={user_id}, name={name})")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6000)
    parser.add_argument("--name", default=f"stage1_{time.time_ns()}")
    parser.add_argument("--password", default="NexusIM_Test_123!")
    parser.add_argument("--timeout", type=float, default=5.0)
    arguments = parser.parse_args()
    run(arguments.host, arguments.port, arguments.name, arguments.password,
        arguments.timeout)
