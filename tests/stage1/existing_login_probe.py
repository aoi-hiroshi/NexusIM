#!/usr/bin/env python3
"""Login an existing account to verify legacy-password migration."""

import argparse
import getpass
import socket

from auth_probe import LOGIN_MSG, LOGIN_MSG_ACK, display_response, receive_json, send_json


def run(host: str, port: int, user_id: int, timeout: float) -> None:
    password = getpass.getpass(f"Password for user {user_id}: ")
    if not password:
        raise ValueError("password must not be empty")

    with socket.create_connection((host, port), timeout=timeout) as connection:
        connection.settimeout(timeout)
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

    print(f"existing_login_probe: PASS (user_id={user_id})")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6000)
    parser.add_argument("--user-id", type=int, required=True)
    parser.add_argument("--timeout", type=float, default=5.0)
    arguments = parser.parse_args()
    run(arguments.host, arguments.port, arguments.user_id, arguments.timeout)
