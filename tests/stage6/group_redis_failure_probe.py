#!/usr/bin/env python3
"""Interactive proof that a group send does not claim Redis real-time delivery."""

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "stage2"))
from cluster_probe import login, logout  # noqa: E402


def run(args: argparse.Namespace) -> None:
    sender, _ = login(args.host, args.port, args.user_id, args.password,
                      args.timeout)
    try:
        input("Stop Redis now, confirm it is unavailable, then press Enter: ")
        request_id = f"stage6-redis-failure-{time.time_ns()}"
        started = time.monotonic()
        sender.send({"msgid": 10, "request_id": request_id,
                     "groupid": args.group_id,
                     "msg": "stage6 redis failure fallback"})
        response = sender.receive()
        elapsed = time.monotonic() - started
        if response.get("msgid") != 27 or response.get("request_id") != request_id:
            raise RuntimeError(f"unexpected group response: {response}")
        if response.get("realtime_routed") is not False:
            raise RuntimeError(f"failure was presented as real-time complete: {response}")
        if response.get("redis_published", 0) != 0:
            raise RuntimeError(f"message was presented as Redis-published: {response}")
        if response.get("queued_offline", 0) < 1 and response.get("failed", 0) < 1:
            raise RuntimeError(f"response omitted fallback/failure outcome: {response}")
        if elapsed > args.max_seconds:
            raise RuntimeError(f"failure handling took too long: {elapsed:.3f}s")
        print("stage6_group_redis_failure_probe: PASS "
              f"seconds={elapsed:.3f} queued={response.get('queued_offline', 0)} "
              f"failed={response.get('failed', 0)}")
    finally:
        try:
            logout(sender)
        except OSError:
            sender.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6001)
    parser.add_argument("--user-id", type=int, required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--group-id", type=int, required=True)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--max-seconds", type=float, default=12.0)
    run(parser.parse_args())
