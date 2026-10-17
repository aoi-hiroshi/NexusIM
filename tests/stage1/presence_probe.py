#!/usr/bin/env python3
"""Verify Redis Presence ownership, renewal, and compare-delete behavior."""

import argparse
import getpass
import socket
import subprocess
import time

from auth_probe import LOGIN_MSG, LOGIN_MSG_ACK, display_response, receive_json, send_json


HEARTBEAT_MSG = 11
HEARTBEAT_MSG_ACK = 12
COMPARE_DELETE_SCRIPT = (
    "if redis.call('GET',KEYS[1]) == ARGV[1] then "
    "return redis.call('DEL',KEYS[1]) else return 0 end"
)


def redis_command(executable: str, host: str, port: int, *arguments: str) -> str:
    result = subprocess.run(
        [executable, "-h", host, "-p", str(port), "--raw", *arguments],
        check=False,
        capture_output=True,
        text=True,
        timeout=5,
    )
    if result.returncode != 0:
        raise RuntimeError(f"redis-cli failed: {result.stderr.strip()}")
    return result.stdout.rstrip("\r\n")


def redis_ttl(executable: str, host: str, port: int, key: str) -> int:
    value = redis_command(executable, host, port, "TTL", key)
    try:
        return int(value)
    except ValueError as error:
        raise RuntimeError(f"Redis returned an invalid TTL for {key}: {value!r}") from error


def wait_until_absent(executable: str, host: str, port: int, key: str,
                      timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if redis_command(executable, host, port, "EXISTS", key) == "0":
            return True
        time.sleep(0.1)
    return redis_command(executable, host, port, "EXISTS", key) == "0"


def run(args: argparse.Namespace) -> None:
    password = getpass.getpass(f"Password for user {args.user_id}: ")
    if not password:
        raise ValueError("password must not be empty")

    key = f"nexusim:presence:user:{args.user_id}"
    with socket.create_connection((args.host, args.port), timeout=args.timeout) as connection:
        connection.settimeout(args.timeout)
        send_json(connection, {
            "msgid": LOGIN_MSG,
            "id": args.user_id,
            "password": password,
        })
        login = receive_json(connection)
        display_response("LOGIN", login)
        if login.get("msgid") != LOGIN_MSG_ACK or login.get("errno") != 0:
            raise RuntimeError(f"login failed: {login}")
        session_id = login.get("session_id", "")
        if len(session_id) != 64:
            raise RuntimeError(f"login returned an invalid session id: {login}")

        owner = redis_command(args.redis_cli, args.redis_host, args.redis_port,
                              "GET", key)
        if not owner.endswith("|" + session_id):
            raise RuntimeError("Redis Presence is not owned by the authenticated session")
        initial_ttl = redis_ttl(args.redis_cli, args.redis_host, args.redis_port, key)
        if initial_ttl <= 0:
            raise RuntimeError(f"Presence has an invalid initial TTL: {initial_ttl}")

        wrong_delete = redis_command(
            args.redis_cli, args.redis_host, args.redis_port,
            "EVAL", COMPARE_DELETE_SCRIPT, "1", key, "stale-session-owner",
        )
        if wrong_delete != "0":
            raise RuntimeError(f"stale compare-delete unexpectedly returned {wrong_delete}")
        if redis_command(args.redis_cli, args.redis_host, args.redis_port,
                         "GET", key) != owner:
            raise RuntimeError("stale compare-delete changed the current Presence owner")

        time.sleep(args.renew_delay)
        ttl_before_renew = redis_ttl(
            args.redis_cli, args.redis_host, args.redis_port, key)
        request_id = f"presence-{time.time_ns()}"
        send_json(connection, {
            "msgid": HEARTBEAT_MSG,
            "request_id": request_id,
        })
        heartbeat = receive_json(connection)
        display_response("HEARTBEAT", heartbeat)
        if heartbeat.get("msgid") != HEARTBEAT_MSG_ACK or \
                heartbeat.get("request_id") != request_id or heartbeat.get("ok") is not True:
            raise RuntimeError(f"heartbeat failed: {heartbeat}")

        renewed_ttl = redis_ttl(args.redis_cli, args.redis_host, args.redis_port, key)
        if renewed_ttl <= ttl_before_renew:
            raise RuntimeError(
                f"Presence TTL was not renewed: before={ttl_before_renew}, after={renewed_ttl}"
            )
        print(
            "PRESENCE: "
            f"initial_ttl={initial_ttl}, before_renew={ttl_before_renew}, "
            f"after_renew={renewed_ttl}, stale_delete=0"
        )

    if not wait_until_absent(args.redis_cli, args.redis_host, args.redis_port,
                             key, args.cleanup_timeout):
        raise RuntimeError("Presence key remained after the owning connection closed")
    print(f"presence_probe: PASS (user_id={args.user_id})")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6000)
    parser.add_argument("--user-id", type=int, required=True)
    parser.add_argument("--redis-host", default="127.0.0.1")
    parser.add_argument("--redis-port", type=int, default=6379)
    parser.add_argument("--redis-cli", default="redis-cli")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--renew-delay", type=float, default=2.1)
    parser.add_argument("--cleanup-timeout", type=float, default=5.0)
    run(parser.parse_args())
