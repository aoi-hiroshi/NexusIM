#!/usr/bin/env python3
"""Exercise Compose service outages and restore every stopped service."""

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests" / "stage2"))
sys.path.insert(0, str(ROOT / "tests" / "stage3"))
sys.path.insert(0, str(ROOT / "tests" / "stage6"))

from cluster_probe import Client, login, logout, register  # noqa: E402
import group_redis_failure_probe  # noqa: E402
import rpc_failure_probe  # noqa: E402

COMPOSE = ["docker", "compose", "--env-file", str(ROOT / ".env"),
           "-f", str(ROOT / "compose.yaml")]


def compose(*args: str) -> None:
    subprocess.run(COMPOSE + list(args), cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)


def healthy(service: str, timeout: float = 40.0) -> None:
    container = subprocess.check_output(
        COMPOSE + ["ps", "-q", service], cwd=ROOT, text=True).strip()
    if not container:
        raise RuntimeError(f"{service} has no container")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = subprocess.check_output(
            ["docker", "inspect", "-f", "{{.State.Health.Status}}", container],
            text=True).strip()
        if status == "healthy":
            return
        if status == "unhealthy":
            raise RuntimeError(f"{service} became unhealthy")
        time.sleep(0.5)
    raise RuntimeError(f"{service} did not become healthy")


def group_redis_outage(state: dict, args: argparse.Namespace) -> None:
    stopped = False

    def stop_redis(_prompt: str) -> None:
        nonlocal stopped
        compose("stop", "redis")
        stopped = True

    try:
        with patch("builtins.input", side_effect=stop_redis):
            group_redis_failure_probe.run(argparse.Namespace(
                host=args.host, port=6001, user_id=state["user_a"],
                password=state["password"], group_id=state["group_id"],
                timeout=15.0, max_seconds=12.0))
    finally:
        if stopped:
            compose("start", "redis")
            healthy("redis")
            compose("restart", "chatserver-1", "chatserver-2")
            healthy("chatserver-1")
            healthy("chatserver-2")


def user_rpc_outage(state: dict, args: argparse.Namespace) -> None:
    stopped = False
    password = state["password"]
    offline_target = register(args.host, 6001,
                              f"stage7_rpc_outage_target_{time.time_ns()}",
                              password, 8.0)
    friend_client, _ = login(args.host, 6001, state["user_a"], password, 8.0)
    chat_client, _ = login(args.host, 6002, state["user_b"], password, 8.0)
    registration_client = Client(args.host, 6001, 5.0)

    def stop_user_service(_prompt: str) -> None:
        nonlocal stopped
        compose("stop", "userservice")
        stopped = True

    try:
        with patch("builtins.input", side_effect=stop_user_service):
            rpc_failure_probe.run(argparse.Namespace(
                host=args.host, port=6001, user_id=state["user_a"],
                password=state["password"], timeout=args.rpc_client_timeout,
                max_seconds=args.rpc_max_seconds))

        cases = (
            (registration_client,
             {"msgid": 4, "name": f"stage7_rpc_down_{time.time_ns()}",
              "password": password}, 5, "Register"),
            (friend_client,
             {"msgid": 7, "request_id": f"stage7-friend-{time.time_ns()}",
              "friendid": 2147483647}, 16, "QueryUser/AddFriend"),
            (chat_client,
             {"msgid": 6, "toid": offline_target,
              "msg": "must not claim delivery during RPC outage"},
             13, "QueryUser/DirectChat"),
        )
        started = time.monotonic()
        for client, request, _, _ in cases:
            client.send(request)
        for client, _, expected_msgid, label in cases:
            response = client.receive()
            if response.get("msgid") != expected_msgid or response.get("error") not in {
                    "RPC_TIMEOUT", "RPC_UNAVAILABLE", "USER_SERVICE_UNAVAILABLE",
                    "ZOOKEEPER_UNAVAILABLE"}:
                raise RuntimeError(f"{label} outage was not explicit: {response}")
            print(f"stage7_{label}_outage: PASS error={response['error']}")
        elapsed = time.monotonic() - started
        if elapsed > 4.5:
            raise RuntimeError(f"RPC method outage exceeded bound: {elapsed:.3f}s")
        print(f"stage7_rpc_method_outages: PASS seconds={elapsed:.3f}")
    finally:
        registration_client.close()
        for client in (friend_client, chat_client):
            try:
                logout(client)
            except OSError:
                client.close()
        if stopped:
            compose("start", "userservice")
            healthy("userservice")


def ai_outage(state: dict, args: argparse.Namespace) -> None:
    stopped = False
    try:
        compose("stop", "aiservice")
        stopped = True
        client, _ = login(args.host, 6001, state["user_a"],
                          state["password"], 8.0)
        try:
            client.socket.settimeout(args.ai_client_timeout)
            request_id = f"stage7-ai-outage-{time.time_ns()}"
            started = time.monotonic()
            client.send({"msgid": 14, "request_id": request_id,
                         "conversation_id": request_id,
                         "prompt": "service outage check"})
            response = client.receive()
            if response.get("msgid") != 15 or \
                    response.get("request_id") != request_id or \
                    response.get("ok") is not False or \
                    response.get("error") != "AI_SERVICE_UNAVAILABLE":
                raise RuntimeError(f"AI outage was not explicit: {response}")
            elapsed = time.monotonic() - started
            print("stage7_ai_service_outage: PASS "
                  f"error=AI_SERVICE_UNAVAILABLE seconds={elapsed:.3f}")
        finally:
            logout(client)
        subprocess.run([sys.executable, str(ROOT / "tests" / "stage2" /
                                              "cluster_probe.py"), "--skip-nginx"],
                       cwd=ROOT, check=True)
        print("stage7_ordinary_chat_without_ai: PASS")
    finally:
        if stopped:
            compose("start", "aiservice")
            healthy("aiservice")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("scenario", choices=("group-redis", "user-rpc", "ai"))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--state-file", default="/tmp/nexusim-stage7-persistence.json")
    parser.add_argument("--rpc-client-timeout", type=float, default=5.0)
    parser.add_argument("--rpc-max-seconds", type=float, default=4.0)
    parser.add_argument("--ai-client-timeout", type=float, default=30.0)
    args = parser.parse_args()
    state = json.loads(Path(args.state_file).read_text(encoding="utf-8"))
    {"group-redis": group_redis_outage,
     "user-rpc": user_rpc_outage,
     "ai": ai_outage}[args.scenario](state, args)


if __name__ == "__main__":
    main()
