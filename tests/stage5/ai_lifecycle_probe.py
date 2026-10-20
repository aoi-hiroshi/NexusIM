#!/usr/bin/env python3
"""Logout during slow AI, relogin, and reject delivery into the new session."""

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "stage2"))
from cluster_probe import HEARTBEAT_MSG, HEARTBEAT_MSG_ACK, login, logout

AI_CHAT_MSG = 14


def run(args: argparse.Namespace) -> None:
    old_client, _ = login(args.host, args.port, args.user_id, args.password,
                          args.timeout)
    stamp = time.time_ns()
    old_client.send({"msgid": AI_CHAT_MSG,
                     "request_id": f"stage5-old-ai-{stamp}",
                     "conversation_id": f"stage5-old-conversation-{stamp}",
                     "prompt": "slow mock request before logout"})
    time.sleep(args.dispatch_wait)
    logout(old_client)

    new_client, _ = login(args.host, args.port, args.user_id, args.password,
                          args.timeout, args.relogin_wait)
    try:
        time.sleep(args.wait_seconds)
        heartbeat_id = f"stage5-new-session-{time.time_ns()}"
        new_client.send({"msgid": HEARTBEAT_MSG, "request_id": heartbeat_id})
        response = new_client.receive()
        if response.get("msgid") != HEARTBEAT_MSG_ACK or \
                response.get("request_id") != heartbeat_id or \
                response.get("ok") is not True:
            raise RuntimeError(
                f"old AI response leaked into the new session: {response}")
        print("stage5_ai_lifecycle_probe: PASS "
              "(old AI response not delivered to new session)")
    finally:
        logout(new_client)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6001)
    parser.add_argument("--user-id", type=int, required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--timeout", type=float, default=95.0)
    parser.add_argument("--wait-seconds", type=float, default=4.0)
    parser.add_argument("--dispatch-wait", type=float, default=0.2)
    parser.add_argument("--relogin-wait", type=float, default=5.0)
    run(parser.parse_args())
