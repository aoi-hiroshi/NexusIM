#!/usr/bin/env python3
"""End-to-end framed JSON AI probe through one real ChatServer."""

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "stage2"))
from cluster_probe import login, logout

AI_CHAT_MSG = 14
AI_CHAT_MSG_ACK = 15


def run(args: argparse.Namespace) -> None:
    client, _ = login(args.host, args.port, args.user_id, args.password,
                      args.timeout)
    request_id = f"stage5-ai-{time.time_ns()}"
    conversation_id = f"stage5-conversation-{args.user_id}-{time.time_ns()}"
    try:
        started = time.monotonic()
        client.send({"msgid": AI_CHAT_MSG, "request_id": request_id,
                     "conversation_id": conversation_id,
                     "prompt": args.prompt,
                     "user_id": 2147483647})
        response = client.receive()
        elapsed = time.monotonic() - started
        if response.get("msgid") != AI_CHAT_MSG_ACK or \
                response.get("request_id") != request_id or \
                response.get("conversation_id") != conversation_id:
            raise RuntimeError(f"unexpected AI response identity: {response}")
        if response.get("ok") is not True:
            raise RuntimeError(f"AI request failed: {response}")
        if not isinstance(response.get("content"), str) or \
                not response["content"].strip():
            raise RuntimeError(f"AI response content is empty: {response}")
        print("stage5_ai_chat_probe: PASS "
              f"model={response.get('model')} seconds={elapsed:.3f} "
              f"content_bytes={len(response['content'].encode('utf-8'))}")
    finally:
        logout(client)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6001)
    parser.add_argument("--user-id", type=int, required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--prompt", default="Reply with a short NexusIM test acknowledgement.")
    parser.add_argument("--timeout", type=float, default=95.0)
    run(parser.parse_args())
