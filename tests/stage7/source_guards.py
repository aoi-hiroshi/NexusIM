#!/usr/bin/env python3
"""Static guards for the Stage 7 deployment and release boundary."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing {label}: {needle}")


compose = read("compose.yaml")
dockerfile = read("deploy/docker/Dockerfile")
schema = read("deploy/docker/mysql/init/001-schema.sql")
provider = read("mprpc/src/rpcprovider.cc")
channel = read("mprpc/src/mprpcchannel.cc")
down_script = read("deploy/scripts/compose-down.sh")
env_example = read("deploy/docker/.env.example")

for service in (
    "mysql:", "redis:", "zookeeper:", "userservice:", "aiservice:",
    "chatserver-1:", "chatserver-2:", "nginx:",
):
    require(compose, service, f"Compose service {service}")

require(compose, "nexusim-mysql-data:/var/lib/mysql", "MySQL named volume")
require(compose, "NEXUSIM_PUBLIC_BIND_IP:-127.0.0.1",
        "safe loopback default for the client entry point")
require(compose, "MPRPC_RPC_ADVERTISE_IP: userservice", "UserService DNS endpoint")
require(compose, "MPRPC_RPC_ADVERTISE_IP: aiservice", "AIService DNS endpoint")
require(compose, "CHAT_REDIS_HOST: redis", "Docker Redis address")
require(compose, "CHAT_DB_HOST: mysql", "Docker MySQL address")
require(provider, 'config.LoadOr("rpcadvertiseip", bindIp)',
        "separate RPC advertised address")
require(provider, 'advertiseIp == "0.0.0.0"', "0.0.0.0 registration rejection")
require(channel, "getaddrinfo", "RPC DNS endpoint resolution")
require(dockerfile, "AS builder", "multi-stage builder")
require(dockerfile, "AS runtime", "multi-stage runtime")
require(dockerfile, "NEXUSIM_BUILD_TESTS=OFF", "production-only image build")
require(schema, "`OfflineMessage`", "fresh offline-message schema")
require(schema, "`uq_friend_user_pair`", "friend uniqueness")
require(schema, "`uq_group_user_pair`", "group membership uniqueness")

if "down -v" in down_script or "down --volumes" in down_script:
    raise AssertionError("ordinary stop script must not delete named volumes")
if "MYSQL_ROOT_PASSWORD" in compose and "CHAT_DB_USER: root" in compose:
    raise AssertionError("application services must not use the MySQL root account")
if "sk-" in compose or "sk-" in env_example:
    raise AssertionError("a real-looking API key was committed")
if "192.168.73.128" in compose:
    raise AssertionError("test-machine IP must not be hard-coded in Compose")

print("stage7_source_guards: PASS")
