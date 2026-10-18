#!/usr/bin/env python3
"""Verify ChatServer returns a bounded error when UserService/ZooKeeper is down."""

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "stage2"))
from cluster_probe import Client, LOGIN_MSG, LOGIN_MSG_ACK


def run(args: argparse.Namespace) -> None:
    input(
        "Stop UserService (or ZooKeeper) now, keep ChatServer running, "
        "then press Enter: "
    )
    client = Client(args.host, args.port, args.timeout)
    try:
        started = time.monotonic()
        client.send({"msgid": LOGIN_MSG, "id": args.user_id,
                     "password": args.password})
        response = client.receive()
        elapsed = time.monotonic() - started
        if response.get("msgid") != LOGIN_MSG_ACK or response.get("errno") == 0:
            raise RuntimeError(f"RPC outage was not reported: {response}")
        if response.get("error") not in {
            "RPC_TIMEOUT", "RPC_UNAVAILABLE", "RPC_SERVER_BUSY",
            "ZOOKEEPER_UNAVAILABLE", "USER_SERVICE_UNAVAILABLE",
        }:
            raise RuntimeError(f"unexpected RPC outage code: {response}")
        if elapsed > args.max_seconds:
            raise RuntimeError(f"outage response took {elapsed:.3f}s")
        print(
            "stage3_rpc_failure_probe: PASS "
            f"(error={response.get('error')}, response_seconds={elapsed:.3f})"
        )
    finally:
        client.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6001)
    parser.add_argument("--user-id", type=int, required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--max-seconds", type=float, default=3.5)
    run(parser.parse_args())
