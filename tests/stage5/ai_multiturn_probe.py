#!/usr/bin/env python3
"""Verify AIService supplies prior successful turns to the context mock."""

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "stage2"))
from cluster_probe import login, logout

AI_CHAT_MSG = 14
AI_CHAT_MSG_ACK = 15


def ask(client, conversation_id: str, prompt: str) -> dict:
    request_id = f"stage5-multiturn-{time.time_ns()}"
    client.send({"msgid": AI_CHAT_MSG, "request_id": request_id,
                 "conversation_id": conversation_id, "prompt": prompt})
    response = client.receive()
    if response.get("msgid") != AI_CHAT_MSG_ACK or \
            response.get("request_id") != request_id or \
            response.get("ok") is not True:
        raise RuntimeError(f"AI turn failed: {response}")
    return response


def run(args: argparse.Namespace) -> None:
    client, _ = login(args.host, args.port, args.user_id, args.password,
                      args.timeout)
    conversation_id = f"stage5-multiturn-{args.user_id}-{time.time_ns()}"
    first_marker = f"first-{time.time_ns()}"
    second_marker = f"second-{time.time_ns()}"
    try:
        first = ask(client, conversation_id, first_marker)
        second = ask(client, conversation_id, second_marker)
        if first_marker not in second.get("content", "") or \
                second_marker not in second.get("content", ""):
            raise RuntimeError(
                f"second response does not contain both context markers: {second}")
        print("stage5_ai_multiturn_probe: PASS")
    finally:
        logout(client)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6001)
    parser.add_argument("--user-id", type=int, required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--timeout", type=float, default=95.0)
    run(parser.parse_args())
