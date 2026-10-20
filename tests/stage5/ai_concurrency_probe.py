#!/usr/bin/env python3
"""With AI_MAX_CONCURRENT_REQUESTS=1 and a slow mock, one of two calls is busy."""

import argparse
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "stage2"))
from cluster_probe import login, logout

AI_CHAT_MSG = 14


def run(args: argparse.Namespace) -> None:
    first, _ = login(args.host, args.port, args.first_user_id,
                     args.first_password, args.timeout)
    second, _ = login(args.host, args.port, args.second_user_id,
                      args.second_password, args.timeout)
    barrier = threading.Barrier(3)
    results = []
    errors = []

    def call(client, user_id):
        try:
            stamp = time.time_ns()
            barrier.wait()
            client.send({"msgid": AI_CHAT_MSG,
                         "request_id": f"stage5-concurrency-{user_id}-{stamp}",
                         "conversation_id": f"stage5-concurrency-{user_id}-{stamp}",
                         "prompt": "slow concurrency mock"})
            results.append(client.receive())
        except Exception as error:
            errors.append(error)

    workers = [threading.Thread(target=call, args=(first, args.first_user_id)),
               threading.Thread(target=call, args=(second, args.second_user_id))]
    try:
        for worker in workers:
            worker.start()
        barrier.wait()
        for worker in workers:
            worker.join(args.timeout)
        if any(worker.is_alive() for worker in workers):
            raise RuntimeError("concurrency calls did not finish")
        if errors:
            raise errors[0]
        ok_count = sum(response.get("ok") is True for response in results)
        busy_count = sum(response.get("error") == "AI_SERVICE_BUSY"
                         for response in results)
        if ok_count != 1 or busy_count != 1:
            raise RuntimeError(f"expected one success and one busy: {results}")
        print("stage5_ai_concurrency_probe: PASS (success=1, busy=1)")
    finally:
        logout(first)
        logout(second)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6001)
    parser.add_argument("--first-user-id", type=int, required=True)
    parser.add_argument("--first-password", required=True)
    parser.add_argument("--second-user-id", type=int, required=True)
    parser.add_argument("--second-password", required=True)
    parser.add_argument("--timeout", type=float, default=95.0)
    run(parser.parse_args())
