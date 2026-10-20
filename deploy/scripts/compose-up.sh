#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
env_file="${NEXUSIM_ENV_FILE:-${repo_root}/.env}"
compose=(docker compose --project-directory "${repo_root}" \
         --env-file "${env_file}" -f "${repo_root}/compose.yaml")

if [[ ! -f "${env_file}" ]]; then
    echo "Missing ${env_file}" >&2
    echo "Copy deploy/docker/.env.example to .env and replace every secret placeholder." >&2
    exit 2
fi
if grep -Eq 'replace-with-(a-distinct-root-secret|an-application-secret|a-redis-secret)' \
        "${env_file}"; then
    echo "Replace all password placeholders in ${env_file} before startup." >&2
    exit 2
fi
if ! command -v docker >/dev/null 2>&1; then
    echo "docker is not installed or not available in PATH" >&2
    exit 2
fi

export NEXUSIM_BUILD_REVISION
NEXUSIM_BUILD_REVISION="$(git -C "${repo_root}" rev-parse --short=12 HEAD 2>/dev/null || printf unknown)"

"${compose[@]}" config --quiet
"${compose[@]}" up -d --build --wait --wait-timeout 600
"${compose[@]}" ps

echo "NexusIM is ready on the port-8000 bind configured in ${env_file}."
echo "Named volumes were preserved. Use compose-down.sh for a normal stop."
