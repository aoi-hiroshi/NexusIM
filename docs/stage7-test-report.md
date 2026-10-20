# NexusIM Stage 7 test report

Date: 2026-10-21
Baseline: `fcd90ac feat: implement friends groups and distributed group chat`

## Scope

Stage 7 adds deployment/release engineering only: multi-stage backend image,
Compose topology, isolated fresh database schema, Docker-aware RPC discovery,
non-destructive operational scripts, Windows Release packaging and final
acceptance probes. It does not add business features.

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

## Ubuntu/VMware status

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

## Release boundary

The system remains an isolated-test V1. There is no TLS, Redis Pub/Sub is not a
durable queue, and complete delivery ACK/retry, global ordering, multi-device
synchronization, streaming AI, RAG and MCP remain out of scope.
