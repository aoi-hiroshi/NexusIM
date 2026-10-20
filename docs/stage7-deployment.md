# NexusIM Stage 7 deployment

## 1. Delivered topology

Stage 7 packages the existing C++ services; it does not replace them with
Mocks or another framework.

```text
Windows Qt 5.12.8 clients
            |
       TCP :8000
            |
       Nginx Stream
        /         \
 ChatServer-1   ChatServer-2
      |  \       /  |
      |   Redis Pub/Sub + Presence
      |             |
      +-- MPRPC / ZooKeeper -- UserService -- MySQL
      +-- MPRPC / ZooKeeper -- AIService -- DeepSeek HTTPS
```

Compose creates the isolated `nexusim-net` bridge. Nginx port 8000 is the only
client entry point and binds to host loopback by default. Ports 6001 and 6002 bind to host loopback for
deterministic local probes; MySQL, Redis, ZooKeeper, UserService and AIService
are not published to the host.

The test-PC layout is:

- Windows: `10.241.23.244`
- VMnet8 host interface: `192.168.73.1`
- Ubuntu VM: `192.168.73.128`
- Qt endpoint: `192.168.73.128:8000`

These addresses are examples in documentation only and do not appear in C++
business code or Compose.

## 2. Reproducible dependencies

The backend image uses Ubuntu 24.04 for builder and runtime stages. Muduo is
compiled from commit
`b55b0ac6a10d4ded1301c3172e779ec46ada2ab3` (the v2.0.2 release).
Protobuf compiler and library come from the same Ubuntu package set. The
Compose services use:

- MySQL `8.4.11`
- Redis `7.4.11-bookworm`
- ZooKeeper `3.9.6-jre-17`
- Nginx `1.31.7`

The runtime image contains only the three installed executables, their exact
dynamic-library closure, CA certificates, a TCP health-check utility and the
Muduo license. Build tools, source code and API keys are absent from the
runtime image.

## 3. First deployment on Ubuntu

Prerequisites:

```bash
docker --version
docker compose version
git --version
python3 --version
```

Prepare secrets:

```bash
cd "$HOME/NexusIM/NexusIM"
cp deploy/docker/.env.example .env
chmod 600 .env
nano .env
```

Replace all three password placeholders. Set `DEEPSEEK_API_KEY` for
the real AI test or leave it empty to verify the explicit
`AI_CONFIG_MISSING` response. On the VMware test PC use:

```dotenv
NEXUSIM_PUBLIC_BIND_IP=192.168.73.128
NEXUSIM_DEBUG_BIND_IP=127.0.0.1
```

The first value deliberately exposes only Nginx on the VM's trusted VMware
interface. The second value intentionally keeps 6001/6002 off the LAN. Change it to
`192.168.73.128` only for a deliberate direct-node Windows test.

Build and start:

```bash
chmod +x deploy/scripts/*.sh deploy/docker/backend/healthcheck.sh
deploy/scripts/compose-up.sh
deploy/scripts/compose-status.sh
```

The first build downloads the fixed Muduo commit and Ubuntu packages, compiles
ChatServer/UserService/AIService from source, initializes the empty container
database and waits for all service health checks.

## 4. Database initialization and isolation

The container MySQL stores data only in the named volume
`nexusim-mysql-data`. It does not mount the host MySQL data directory
and cannot alter the Ubuntu host's existing `chat` or
`nexusim` schemas.

On the first initialization only, MySQL executes:

1. `deploy/docker/mysql/init/001-schema.sql`
2. `chatserver/sql/003_stage6_relationship_constraints.sql`

The schema uses the real case-sensitive table names `User`,
`Friend`, `AllGroup`, `GroupUser` and
`OfflineMessage`. The application account defaults to
`nexusim_app`; root credentials are never passed to a C++ service.
Registration writes only PBKDF2-SHA256 password representations through
UserService.

Initialization scripts do not rerun against a non-empty volume. Ordinary stop:

```bash
deploy/scripts/compose-down.sh
```

This command never uses `down -v`. Deleting a named volume permanently
destroys container data and is intentionally not automated.

## 5. RPC discovery inside Docker

`MPRPC_RPC_SERVER_IP` is the bind address and may be
`0.0.0.0`. `MPRPC_RPC_ADVERTISE_IP` is the endpoint stored
in ZooKeeper and is `userservice` or `aiservice`.
RpcProvider refuses to advertise `0.0.0.0`, while MprpcChannel now
resolves IPv4 DNS names on its bounded business worker.

Inspect registrations:

```bash
docker compose exec zookeeper zkCli.sh get /nexusim.user.UserService/Login
docker compose exec zookeeper zkCli.sh get /nexusim.ai.AIService/Chat
```

Expected values are `userservice:7001` and
`aiservice:7002`.

## 6. Nginx and logs

The Nginx configuration uses the stream module, round-robin upstreams and
Docker DNS re-resolution. It does not parse the four-byte big-endian framed
JSON protocol.

```bash
docker compose exec nginx nginx -t
deploy/scripts/compose-logs.sh nginx
deploy/scripts/compose-logs.sh chatserver-1 chatserver-2
deploy/scripts/compose-logs.sh userservice aiservice
```

If one ChatServer is stopped, Nginx should route new TCP connections to the
healthy node:

```bash
docker compose stop chatserver-1
# establish new clients through port 8000
docker compose start chatserver-1
```

Existing connections assigned to the stopped node are disconnected; V1 does
not provide transparent session migration.

## 7. Native Linux build

Docker is optional for source compilation:

```bash
cmake --fresh -S . -B build/stage7 -DCMAKE_BUILD_TYPE=Release
cmake --build build/stage7 -j"$(nproc)" \
  --target ChatServer UserService AIService
ctest --test-dir build/stage7 --output-on-failure
python3 tests/stage7/source_guards.py
```

The native build expects Muduo, Protobuf, ZooKeeper C client, hiredis, MySQL
client, OpenSSL and libcurl development packages installed on Ubuntu.

## 8. Windows Qt 5.12.8 release

On the Windows development/test PC:

```powershell
powershell -ExecutionPolicy Bypass -File client/qt/release.ps1 `
  -Version 1.0.0-stage7 `
  -QtRoot E:\Qt\Qt5.12.8\5.12.8\msvc2017_64
```

This performs a VS2019 x64 Release build, runs QtTest, calls
`windeployqt`, copies the VC142 runtime, generates
`SHA256SUMS.txt`, and creates a ZIP plus a separate SHA-256 file.
No API key or server password is placed in the package.

Launch two clients:

```powershell
Start-Process .\dist\NexusIMClient-1.0.0-stage7-win64\NexusIMClient.exe `
  -ArgumentList "--host","192.168.73.128","--port","8000"
Start-Process .\dist\NexusIMClient-1.0.0-stage7-win64\NexusIMClient.exe `
  -ArgumentList "--host","192.168.73.128","--port","8000"
```

## 9. Security and reliability boundary

- V1 has no TLS; deploy only on the isolated VMware/LAN test network.
- Compose `.env` is ignored by Git but environment variables remain
  visible to the local Docker administrator. Production would require a real
  secret manager.
- Redis Pub/Sub is real-time best effort, not durable delivery.
- No full message ACK/retry protocol, global ordering, multi-device session
  synchronization, streaming AI, RAG or MCP is claimed.
- Do not expose 3306, 6379, 2181, 7001 or 7002 publicly.
