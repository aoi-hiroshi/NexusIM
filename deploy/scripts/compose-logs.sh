#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
env_file="${NEXUSIM_ENV_FILE:-${repo_root}/.env}"

docker compose --project-directory "${repo_root}" \
    --env-file "${env_file}" -f "${repo_root}/compose.yaml" \
    logs --tail "${NEXUSIM_LOG_TAIL:-200}" "$@"
