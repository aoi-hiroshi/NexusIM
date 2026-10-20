#!/usr/bin/env python3
"""Test-only OpenAI-compatible HTTP mock. It never calls DeepSeek."""

import argparse
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    scenario = "normal"
    delay_seconds = 3.0

    def log_message(self, fmt, *args):
        print("mock_deepseek:", fmt % args)

    def do_POST(self):
        if self.path != "/chat/completions":
            self.send_error(404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            request = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(request.get("messages"), list):
                raise ValueError("messages missing")
        except (ValueError, UnicodeDecodeError, json.JSONDecodeError):
            self._json(400, {"error": {"message": "bad mock request"}})
            return

        if self.scenario == "timeout":
            time.sleep(self.delay_seconds)
        if self.scenario in {"401", "429", "500"}:
            status = int(self.scenario)
            self._json(status, {"error": {"message": "synthetic mock error"}})
            return
        if self.scenario == "invalid-json":
            self._raw(200, b"{not-json")
            return
        if self.scenario == "empty-choices":
            self._json(200, {"id": "mock-empty", "model": "deepseek-v4-pro",
                             "choices": []})
            return
        if self.scenario == "empty-content":
            content = ""
        elif self.scenario == "long":
            content = "NexusIM long mock reply. " * 12000
        elif self.scenario == "unicode":
            content = "中文回复 🤖\n特殊字符: <>&\"'"
        elif self.scenario == "context":
            content = "context=" + "|".join(
                message.get("content", "")
                for message in request["messages"]
                if message.get("role") == "user")
        else:
            content = "NexusIM mock reply (not a real DeepSeek response)."
        self._json(200, {
            "id": "mock-request-id",
            "model": "deepseek-v4-pro",
            "choices": [{"message": {"role": "assistant", "content": content},
                         "finish_reason": "stop"}],
            "usage": {"prompt_tokens": 7, "completion_tokens": 5,
                      "total_tokens": 12},
        }, {"X-Request-Id": "mock-header-id"})

    def _json(self, status, value, headers=None):
        self._raw(status, json.dumps(value, ensure_ascii=False).encode("utf-8"),
                  headers)

    def _raw(self, status, body, headers=None):
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--scenario", choices=(
        "normal", "401", "429", "500", "timeout", "invalid-json",
        "empty-choices", "empty-content", "long", "unicode", "context"),
        default="normal")
    parser.add_argument("--delay-seconds", type=float, default=3.0)
    args = parser.parse_args()
    Handler.scenario = args.scenario
    Handler.delay_seconds = args.delay_seconds
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"mock_deepseek: http://{args.host}:{args.port} scenario={args.scenario}")
    server.serve_forever()


if __name__ == "__main__":
    main()
