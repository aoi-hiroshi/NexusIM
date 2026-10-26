# NexusIM Stage 7 test report

Date: 2026-10-21
Baseline: `fcd90ac feat: implement friends groups and distributed group chat`

## Scope

Stage 7 adds deployment/release engineering only: multi-stage backend image,
Compose topology, isolated fresh database schema, Docker-aware RPC discovery,
non-destructive operational scripts, Windows Release packaging and final
acceptance probes. It does not add business features. Ubuntu acceptance found
two compatibility defects and an RPC timeout regression; their targeted fixes
are recorded below.

## Results recorded on the Windows development machine

All passing rows below were produced from the Stage 7 working tree on
2026-10-21. A passing source/configuration check is not presented as a Docker
runtime test.

| Check | Result |
|---|---|
| Stage 7 Python source guards | PASS |
| Stage 2/3/5/6 and Qt source guards | PASS |
| Stage 6 portable protocol tests | PASS, 1/1 test, 0.04 s (latest rerun) |
| Qt 5.12.8 + VS2019 Release build | PASS |
| QtTest Release regression | PASS, 8/8 tests, 3.23 s (final release run) |
| Shell syntax (`bash -n`) | PASS |
| PowerShell parser validation | PASS |
| Compose YAML structural parse | PASS, 8 service instances |
| Repository secret-pattern scan | PASS |
| Stage 7 ZIP/package manifest | PASS, 36 files |
| Packaged client smoke launch | PASS; process remained running until test shutdown |
| Docker/Compose execution | Not available on this Windows development machine |

The release command was:

```powershell
powershell -ExecutionPolicy Bypass -File client/qt/release.ps1 `
  -Version 1.0.0-stage7
```

It generated:

- `dist/NexusIMClient-1.0.0-stage7-win64/`
- `dist/NexusIMClient-1.0.0-stage7-win64.zip`
- `dist/NexusIMClient-1.0.0-stage7-win64.zip.sha256`

Recorded ZIP size: `46,687,555` bytes. Recorded and independently recomputed
SHA-256:

```text
adc371e80739c646327d12990ca76918c4cdff2522592ac4b01c19d85db62a92
```

The package check confirmed `NexusIMClient.exe`, `Qt5Core.dll`,
`Qt5Network.dll`, `Qt5Widgets.dll`, `platforms/qwindows.dll`, the runtime
README, third-party notices and the per-file checksum manifest.

### Issue found and resolved during verification

The first Release CTest attempt returned Windows loader error `0xc0000135`
for all eight Qt tests because the Qt runtime directory was not present in the
test process `PATH`. `client/qt/release.ps1` now prepends the selected Qt
`bin` directory before CTest. The complete rerun then built successfully and
passed 8/8 tests. This was a packaging/test-environment issue, not a test
expectation change.

## Ubuntu/VMware status as of 2026-10-21

No Stage 7 Docker command has been executed on the Ubuntu test machine yet.
Therefore this report does not claim:

- successful backend image compilation;
- successful Compose startup or health checks;
- MySQL initialization/persistence;
- Docker DNS/ZooKeeper registration;
- Stage 6 friend/group Linux validation;
- Nginx failover;
- real DeepSeek API access from AIService in a container.

The exact commands and evidence checklist are in
`docs/stage7-acceptance.md`. Results from the test PC must be appended
after execution.

## Ubuntu/VMware results on 2026-10-26

The Ubuntu 26.04 VM was tested over SSH in an isolated worktree based on
`1ec4727` plus the fixes in this report. The original VM checkout's generated
files and runtime configuration were preserved. Docker 29.1.3 and Compose
2.40.3 were installed before this run. No secret values were committed.

| Check | Actual result |
|---|---|
| Native Linux build | ChatServer, UserService, AIService and the test targets built successfully |
| CTest | 5/5 passed: Stage 3 frame/protocol, Stage 5 AI core, Stage 6 protocol, Stage 7 source guards |
| Backend and Nginx images | Built successfully on Ubuntu 24.04 bases |
| Compose startup | 8/8 services healthy; 6001, 6002 and 8000 bound to `127.0.0.1` |
| ZooKeeper RPC discovery | Login/Register/QueryUser = `userservice:7001`; AI Chat = `aiservice:7002` |
| Redis subscriptions | One subscriber on each node channel |
| `compose-verify.sh` | PASS after the final fixes, including Stage 1/2/6 black-box probes and real AI through port 8000 |
| Persistent data | `persistence_probe.py prepare`, Compose down/up without `-v`, then `verify`: PASS |
| Nginx node failure | With chatserver-1 stopped, a new port-8000 login reached chatserver-2; node 1 restored healthy |
| Redis failure | Group send reported `queued=1`, `failed=0`, `redis_published=0`; Redis and both ChatServers restored healthy |
| UserService failure | Login returned `RPC_TIMEOUT` in 2.011 s; Register, QueryUser/AddFriend and QueryUser/DirectChat each returned explicit `RPC_TIMEOUT` within a 2.012 s three-method window; UserService restored healthy |
| AIService failure | Returned `AI_SERVICE_UNAVAILABLE` in 8.009 s; ordinary cross-node chat passed while AI was stopped |
| Real DeepSeek API | Container request through Nginx passed with `deepseek-v4-pro`, 1.477 s in the post-fault final regression |

The post-fault `compose-verify.sh` run reported: framed protocol PASS, session
security PASS, local and cross-node direct chat PASS, duplicate login PASS,
Presence PASS, Nginx distribution `3/3`, Stage 6 friend/group probe PASS,
and real AI chat PASS (`group_id=5`). The script ended with
`Stage 7 Docker black-box regression: PASS` and exit code 0.

### Issues discovered and repaired on Ubuntu

1. Ubuntu's MySQL C library does not expose `mysql_stmt_get_result`.
   Prepared SELECTs now use portable bind/fetch APIs and RAII result storage;
   native and container business probes both passed afterward.
2. Docker Hub and GitHub Git transport were unreachable from the VM. The
   pinned base images were retrieved from a reachable public registry for this
   test. The Dockerfile now downloads the pinned Muduo archive from GitHub
   codeload and verifies its SHA-256 before building it. The image build limits
   parallel compilation to two jobs on this 3.3 GiB VM.
3. The pinned Nginx image has stream compiled in statically. The image check
   and configuration were corrected; final `nginx -t` succeeded without the
   earlier worker-connection warning.
4. The first UserService outage test waited beyond the RPC deadline without a
   client response. ChatServer now schedules a per-request EventLoop timeout
   that sends an explicit error while the blocking worker is still occupied.
   The repaired container returned `RPC_TIMEOUT` in 2.011 s. Register and both
   reachable QueryUser paths also returned bounded errors. Late worker
   completion cannot send a second reply for the same request.
5. The first AI outage probe used an 8-second socket timeout and timed out.
   With a 30-second test timeout, the service returned the expected explicit
   error in 8.009 s. This is a bounded failure response, but the 8-second
   discovery delay remains a V1 latency limitation.

The VM's Compose stack was left running and healthy after verification. Its
port-8000 bind is loopback-only. The final Windows packaged-client demonstration
against this specific Compose deployment was not run in this session.

## Release boundary

The system remains an isolated-test V1. There is no TLS, Redis Pub/Sub is not a
durable queue, and complete delivery ACK/retry, global ordering, multi-device
synchronization, streaming AI, RAG and MCP remain out of scope.
