# NexusIM Stage 7 final acceptance

Run these checks on the Ubuntu VMware test machine after Stage 6 verification.
Record literal output; do not convert a skipped or Mock case into a PASS.

## A. Static and native build

```bash
cd "$HOME/NexusIM/NexusIM"
git status --short --branch
git log -1 --oneline

python3 tests/stage7/source_guards.py
cmake --fresh -S . -B build/stage7 -DCMAKE_BUILD_TYPE=Release
cmake --build build/stage7 -j"$(nproc)" \
  --target ChatServer UserService AIService
ctest --test-dir build/stage7 --output-on-failure
```

Acceptance: clean configure/build, all CTest cases pass, and no generated
binary is taken from the original course snapshot.

## B. Compose configuration and startup

```bash
cp deploy/docker/.env.example .env
chmod 600 .env
nano .env

docker compose --env-file .env -f compose.yaml config --quiet
deploy/scripts/compose-up.sh
docker compose ps
docker compose exec nginx nginx -t
```

Acceptance: eight containers are running/healthy (two ChatServers plus six
support/services), port 8000 listens on the VM, and 6001/6002 follow the chosen
debug bind address.

## C. Service discovery and cluster wiring

```bash
docker compose exec zookeeper zkCli.sh get /nexusim.user.UserService/Login
docker compose exec zookeeper zkCli.sh get /nexusim.user.UserService/Register
docker compose exec zookeeper zkCli.sh get /nexusim.user.UserService/QueryUser
docker compose exec zookeeper zkCli.sh get /nexusim.ai.AIService/Chat

deploy/scripts/compose-status.sh
```

Acceptance: UserService methods resolve to `userservice:7001`,
AIService resolves to `aiservice:7002`, and both node channels have
one Redis subscriber.

## D. Protocol, security and business regression

```bash
deploy/scripts/compose-verify.sh
```

This runs the Stage 1 framed protocol/security probes, Stage 2 two-node/Nginx
probe and Stage 6 friend/group/distributed-group-chat probe. To include a real
AI request:

```bash
STAGE7_AI_USER_ID=<existing-id> \
STAGE7_AI_PASSWORD='<password>' \
deploy/scripts/compose-verify.sh
```

## E. Named-volume persistence

```bash
python3 tests/stage7/persistence_probe.py prepare
deploy/scripts/compose-down.sh
deploy/scripts/compose-up.sh
python3 tests/stage7/persistence_probe.py verify
```

Acceptance: the same user accounts can log in after container recreation and
the friend relationship, group, and group memberships are preserved.

## F. Fault behavior

Perform each case separately and restore the service before continuing:

1. Stop `chatserver-1`; new Nginx connections reach
   `chatserver-2`.
2. Stop Redis; ChatServer remains alive and does not claim failed cross-node
   real-time delivery as successful.
3. Stop UserService; login returns a bounded RPC/service error.
4. Stop AIService; ordinary chat still works and AI returns
   `AI_SERVICE_UNAVAILABLE`.
5. Start each service again and repeat the basic login/chat probe.

Do not use `docker compose down -v` during fault tests.

Automated Linux probes for the four cases above (run after section E created
`/tmp/nexusim-stage7-persistence.json`):

```bash
docker compose --env-file .env -f compose.yaml stop chatserver-1
python3 tests/stage7/nginx_failover_probe.py --expected-node chatserver-2
docker compose --env-file .env -f compose.yaml start chatserver-1

python3 tests/stage7/compose_fault_probe.py group-redis
python3 tests/stage7/compose_fault_probe.py user-rpc
python3 tests/stage7/compose_fault_probe.py ai
```

The three `compose_fault_probe.py` scenarios restore their stopped service in
`finally`. The Redis scenario also restarts both ChatServers so their command
and subscription connections are re-established. Check `docker compose ps`
afterward.

## G. Windows release and dual-client demonstration

1. Run `client/qt/release.ps1`.
2. Verify the ZIP against its `.sha256` file.
3. Start two packaged clients and connect both to
   `192.168.73.128:8000`.
4. Demonstrate register/login, friend addition, direct chat, group
   create/join/group chat, offline recovery, reconnect, and AI chat.
5. Capture Nginx/ChatServer logs showing allocation across both nodes.

## H. Final evidence table

| Case | Required evidence | Status before Ubuntu run |
|---|---|---|
| Backend image build | Full build exit code and image ID | Pending |
| Compose health | `docker compose ps` | Pending |
| RPC discovery | Four ZooKeeper values | Pending |
| TCP/security regression | Probe outputs | Pending |
| Direct/cross-node chat | Stage 2 probe | Pending |
| Friends/groups/group chat | Stage 6 probe | Pending |
| AI real API | Stage 5 probe with model/timing | Pending |
| Persistence | Prepare + recreate + verify | Pending |
| Node/service failures | Explicit error/failover outputs | Pending |
| Windows package | CTest, ZIP hash, two-client evidence | Build/CTest/ZIP PASS; two-client Stage 7 package demo pending |
