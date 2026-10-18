#!/usr/bin/env python3
"""Keep one login RPC slow while another authenticated connection heartbeats."""

import argparse
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "stage2"))
from cluster_probe import Client, LOGIN_MSG, heartbeat, login, logout


def run(args: argparse.Namespace) -> None:
    healthy, _ = login(args.host, args.port, args.healthy_user_id,
                       args.healthy_password, args.timeout)
    slow = Client(args.host, args.port, args.timeout)
    slow_result = {}

    def slow_login() -> None:
        try:
            slow.send({"msgid": LOGIN_MSG, "id": args.slow_user_id,
                       "password": args.slow_password})
            slow_result["response"] = slow.receive()
        except Exception as error:  # surfaced in the main thread below
            slow_result["error"] = error

    worker = threading.Thread(target=slow_login)
    try:
        worker.start()
        time.sleep(args.startup_wait)
        started = time.monotonic()
        heartbeat(healthy, "during-slow-rpc")
        elapsed = time.monotonic() - started
        if elapsed > args.max_heartbeat_seconds:
            raise RuntimeError(f"heartbeat was delayed for {elapsed:.3f}s")
        worker.join(args.timeout)
        if worker.is_alive():
            raise RuntimeError("slow login did not finish within the client timeout")
        if "error" in slow_result:
            raise slow_result["error"]
        print(
            "stage3_eventloop_nonblocking_probe: PASS "
            f"(heartbeat_seconds={elapsed:.3f}, slow_response={slow_result.get('response')})"
        )
    finally:
        slow.close()
        logout(healthy)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6001)
    parser.add_argument("--slow-user-id", type=int, required=True)
    parser.add_argument("--slow-password", required=True)
    parser.add_argument("--healthy-user-id", type=int, required=True)
    parser.add_argument("--healthy-password", required=True)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--startup-wait", type=float, default=0.3)
    parser.add_argument("--max-heartbeat-seconds", type=float, default=1.5)
    run(parser.parse_args())
