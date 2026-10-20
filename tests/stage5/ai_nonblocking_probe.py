#!/usr/bin/env python3
"""Verify a slow AI call does not block heartbeat on the same connection."""

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "stage2"))
from cluster_probe import HEARTBEAT_MSG, HEARTBEAT_MSG_ACK, login, logout

AI_CHAT_MSG = 14
AI_CHAT_MSG_ACK = 15


def run(args: argparse.Namespace) -> None:
    client, _ = login(args.host, args.port, args.user_id, args.password,
                      args.timeout)
    stamp = time.time_ns()
    ai_request_id = f"stage5-slow-ai-{stamp}"
    heartbeat_id = f"stage5-heartbeat-{stamp}"
    try:
        client.send({"msgid": AI_CHAT_MSG, "request_id": ai_request_id,
                     "conversation_id": f"stage5-slow-{args.user_id}-{stamp}",
                     "prompt": "slow mock request"})
        started = time.monotonic()
        client.send({"msgid": HEARTBEAT_MSG, "request_id": heartbeat_id})
        heartbeat_seconds = None
        ai_response = None
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline and (heartbeat_seconds is None or ai_response is None):
            response = client.receive()
            if response.get("msgid") == HEARTBEAT_MSG_ACK and \
                    response.get("request_id") == heartbeat_id:
                if response.get("ok") is not True:
                    raise RuntimeError(f"heartbeat failed: {response}")
                heartbeat_seconds = time.monotonic() - started
            elif response.get("msgid") == AI_CHAT_MSG_ACK and \
                    response.get("request_id") == ai_request_id:
                ai_response = response
            else:
                raise RuntimeError(f"unexpected response: {response}")
        if heartbeat_seconds is None:
            raise RuntimeError("heartbeat response was not received")
        if heartbeat_seconds > args.max_heartbeat_seconds:
            raise RuntimeError(
                f"heartbeat was blocked by AI for {heartbeat_seconds:.3f}s")
        if ai_response is None:
            raise RuntimeError("AI response was not received")
        print("stage5_ai_nonblocking_probe: PASS "
              f"heartbeat_seconds={heartbeat_seconds:.3f} "
              f"ai_ok={ai_response.get('ok')}")
    finally:
        logout(client)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6001)
    parser.add_argument("--user-id", type=int, required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--timeout", type=float, default=95.0)
    parser.add_argument("--max-heartbeat-seconds", type=float, default=1.5)
    run(parser.parse_args())
