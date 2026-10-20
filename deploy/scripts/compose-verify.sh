#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
env_file="${NEXUSIM_ENV_FILE:-${repo_root}/.env}"
compose=(docker compose --project-directory "${repo_root}" \
         --env-file "${env_file}" -f "${repo_root}/compose.yaml")

cd "${repo_root}"
"${compose[@]}" config --quiet
"${compose[@]}" ps
"${compose[@]}" exec -T nginx nginx -t

python3 tests/stage1/server_frame_probe.py --host 127.0.0.1 --port 6001
python3 tests/stage1/session_security_probe.py --host 127.0.0.1 --port 6001
python3 tests/stage2/cluster_probe.py
python3 tests/stage6/social_group_probe.py \
    --host 127.0.0.1 --node1-port 6001 --node2-port 6002

if [[ -n "${STAGE7_AI_USER_ID:-}" && -n "${STAGE7_AI_PASSWORD:-}" ]]; then
    python3 tests/stage5/ai_chat_probe.py \
        --host 127.0.0.1 --port 8000 \
        --user-id "${STAGE7_AI_USER_ID}" \
        --password "${STAGE7_AI_PASSWORD}"
else
    echo "AI live probe: SKIPPED (set STAGE7_AI_USER_ID and STAGE7_AI_PASSWORD)"
fi

echo "Stage 7 Docker black-box regression: PASS"
