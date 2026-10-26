#!/usr/bin/env python3
"""Verify that new Nginx connections reach the surviving ChatServer."""

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "stage2"))
from cluster_probe import login, logout, register  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--expected-node", required=True)
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()

    password = "NexusIM_Stage7_Failover_123!"
    user_id = register(args.host, args.port,
                       f"stage7_failover_{time.time_ns()}",
                       password, args.timeout)
    client, response = login(args.host, args.port, user_id,
                             password, args.timeout)
    try:
        node_id = response.get("node_id")
        if node_id != args.expected_node:
            raise RuntimeError(
                f"expected {args.expected_node}, got {node_id!r}")
        print(f"stage7_nginx_failover_probe: PASS node_id={node_id}")
    finally:
        logout(client)


if __name__ == "__main__":
    main()
