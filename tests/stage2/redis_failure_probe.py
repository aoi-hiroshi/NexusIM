#!/usr/bin/env python3
"""Controlled Redis-outage probe for Stage 2.

The operator is prompted to stop Redis after two sessions are online. This is
intentionally not hidden automation: the script never stops infrastructure.
"""

import argparse
import socket
import time

from cluster_probe import (
    Client,
    ERROR_MSG,
    HEARTBEAT_MSG,
    HEARTBEAT_MSG_ACK,
    ONE_CHAT_MSG,
    login,
    logout,
    register,
)


def run(args: argparse.Namespace) -> None:
    password = "NexusIM_Stage2_Outage_123!"
    suffix = time.time_ns()
    user_a = register(args.host, args.node1_port, f"stage2_fail_a_{suffix}",
                      password, args.timeout)
    user_b = register(args.host, args.node1_port, f"stage2_fail_b_{suffix}",
                      password, args.timeout)
    client_a, _ = login(args.host, args.node1_port, user_a, password, args.timeout)
    client_b, _ = login(args.host, args.node2_port, user_b, password, args.timeout)

    try:
        input("Stop Redis now, confirm it is unavailable, then press Enter: ")
        started = time.monotonic()
        client_a.send({"msgid": ONE_CHAT_MSG, "toid": user_b,
                       "msg": "must-not-be-reported-as-realtime"})
        response = client_a.receive()
        elapsed = time.monotonic() - started
        if response.get("msgid") != ERROR_MSG or response.get("error") not in {
                "PRESENCE_UNAVAILABLE", "REDIS_DELIVERY_FAILED"}:
            raise RuntimeError(f"outage was not reported explicitly: {response}")
        if response.get("queued_offline") is not True:
            raise RuntimeError(f"outage fallback was not reported accurately: {response}")
        if elapsed > args.max_response_seconds:
            raise RuntimeError(f"Redis failure response took {elapsed:.2f}s")

        client_b.socket.settimeout(1.0)
        try:
            unexpected = client_b.receive()
        except socket.timeout:
            unexpected = None
        if unexpected is not None:
            raise RuntimeError(f"message unexpectedly arrived in real time: {unexpected}")

        request_id = f"redis-down-{time.time_ns()}"
        client_a.socket.settimeout(args.timeout)
        client_a.send({"msgid": HEARTBEAT_MSG, "request_id": request_id})
        heartbeat = client_a.receive()
        if heartbeat.get("msgid") != HEARTBEAT_MSG_ACK or \
                heartbeat.get("request_id") != request_id or \
                heartbeat.get("ok") is not False or \
                heartbeat.get("error") != "PRESENCE_LOST":
            raise RuntimeError(f"heartbeat did not fail closed: {heartbeat}")

        print(
            "redis_failure_probe: PASS "
            f"(explicit={response.get('error')}, queued_offline=true, "
            f"response_seconds={elapsed:.3f})"
        )
    finally:
        client_a.close()
        client_b.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--node1-port", type=int, default=6001)
    parser.add_argument("--node2-port", type=int, default=6002)
    parser.add_argument("--timeout", type=float, default=6.0)
    parser.add_argument("--max-response-seconds", type=float, default=5.0)
    run(parser.parse_args())
