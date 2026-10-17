#!/usr/bin/env python3
"""Observe TTL cleanup after the operator kills the owning ChatServer node."""

import argparse
import getpass
import os
import subprocess
import time

from cluster_probe import login


def redis_command(args: argparse.Namespace, *command: str) -> str:
    environment = os.environ.copy()
    password = environment.get("CHAT_REDIS_PASSWORD", "")
    if password:
        environment["REDISCLI_AUTH"] = password
    call = [args.redis_cli, "-h", args.redis_host, "-p",
            str(args.redis_port), "--raw"]
    username = environment.get("CHAT_REDIS_USERNAME", "")
    if username:
        call.extend(["--user", username])
    result = subprocess.run(call + list(command), check=False,
                            capture_output=True, text=True,
                            timeout=args.command_timeout, env=environment)
    if result.returncode != 0:
        raise RuntimeError(f"redis-cli failed: {result.stderr.strip()}")
    return result.stdout.rstrip("\r\n")


def run(args: argparse.Namespace) -> None:
    password = getpass.getpass(f"Password for user {args.user_id}: ")
    client, response = login(args.host, args.port, args.user_id, password,
                             args.command_timeout)
    key = f"nexusim:presence:user:{args.user_id}"
    owner = redis_command(args, "GET", key)
    if not owner.endswith("|" + response["session_id"]):
        client.close()
        raise RuntimeError("Presence key does not belong to this login")
    initial_ttl = int(redis_command(args, "TTL", key))
    if initial_ttl <= 0:
        client.close()
        raise RuntimeError(f"invalid initial TTL: {initial_ttl}")

    input(
        f"Kill the owning ChatServer process for node {response.get('node_id')} "
        "with SIGKILL, do not stop Redis, then press Enter: "
    )
    client.close()
    deadline = time.monotonic() + initial_ttl + args.grace_seconds
    while time.monotonic() < deadline:
        if redis_command(args, "EXISTS", key) == "0":
            print(
                "presence_crash_probe: PASS "
                f"(key expired after owner crash, initial_ttl={initial_ttl})"
            )
            return
        time.sleep(args.poll_seconds)
    raise RuntimeError("Presence did not expire within TTL plus grace interval")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6001)
    parser.add_argument("--user-id", type=int, required=True)
    parser.add_argument("--redis-host", default="127.0.0.1")
    parser.add_argument("--redis-port", type=int, default=6379)
    parser.add_argument("--redis-cli", default="redis-cli")
    parser.add_argument("--command-timeout", type=float, default=5.0)
    parser.add_argument("--poll-seconds", type=float, default=0.5)
    parser.add_argument("--grace-seconds", type=float, default=5.0)
    run(parser.parse_args())
