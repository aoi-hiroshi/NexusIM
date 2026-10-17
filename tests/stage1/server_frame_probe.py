#!/usr/bin/env python3
"""Black-box FrameCodec probe for a running ChatServer; no database login needed."""

import argparse
import json
import socket
import struct


def frame(payload: bytes) -> bytes:
    return struct.pack("!I", len(payload)) + payload


def receive_frame(connection: socket.socket) -> dict:
    header = receive_exact(connection, 4)
    length = struct.unpack("!I", header)[0]
    assert 0 < length <= 1024 * 1024, f"invalid response length {length}"
    return json.loads(receive_exact(connection, length).decode("utf-8"))


def receive_exact(connection: socket.socket, size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        chunk = connection.recv(size - len(result))
        if not chunk:
            raise AssertionError("connection closed before a complete response")
        result.extend(chunk)
    return bytes(result)


def connect(host: str, port: int) -> socket.socket:
    connection = socket.create_connection((host, port), timeout=3)
    connection.settimeout(3)
    return connection


def run(host: str, port: int) -> None:
    # Deliver one invalid JSON frame one byte at a time (half-packet accumulation).
    with connect(host, port) as connection:
        encoded = frame(b"not-json")
        for byte in encoded:
            connection.sendall(bytes([byte]))
        response = receive_frame(connection)
        assert response["error"] == "INVALID_JSON", response

    # Deliver two frames in one write (sticky packets / multi-frame loop).
    with connect(host, port) as connection:
        connection.sendall(frame(b"bad-1") + frame(b"bad-2"))
        responses = [receive_frame(connection), receive_frame(connection)]
        assert all(value["error"] == "INVALID_JSON" for value in responses), responses

    # A zero length is illegal and must result in an error followed by shutdown.
    with connect(host, port) as connection:
        connection.sendall(struct.pack("!I", 0))
        response = receive_frame(connection)
        assert response["error"] == "INVALID_FRAME", response

    # Oversize is rejected from the header without waiting for a payload allocation.
    with connect(host, port) as connection:
        connection.sendall(struct.pack("!I", 1024 * 1024 + 1))
        response = receive_frame(connection)
        assert response["error"] == "INVALID_FRAME", response

    print("server_frame_probe: PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6000)
    arguments = parser.parse_args()
    run(arguments.host, arguments.port)
