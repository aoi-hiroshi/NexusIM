#!/usr/bin/env python3
"""Disconnect during a delayed Login RPC and verify no Presence is committed."""

import argparse
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "stage2"))
from cluster_probe import Client, LOGIN_MSG


def run(args: argparse.Namespace) -> None:
    client = Client(args.host, args.port, args.timeout)
    client.send({"msgid": LOGIN_MSG, "id": args.user_id,
                 "password": args.password})
    client.close()
    time.sleep(args.wait_seconds)

    key = f"nexusim:presence:user:{args.user_id}"
    command = ["redis-cli", "-h", args.redis_host, "-p",
               str(args.redis_port), "GET", key]
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    value = result.stdout.strip()
    if value and value != "(nil)":
        raise RuntimeError(f"stale Presence remained after disconnect: {value}")
    print("stage3_rpc_lifecycle_probe: PASS (no Presence after stale RPC)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6001)
    parser.add_argument("--user-id", type=int, required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--wait-seconds", type=float, default=4.0)
    parser.add_argument("--redis-host", default="127.0.0.1")
    parser.add_argument("--redis-port", type=int, default=6379)
    run(parser.parse_args())
