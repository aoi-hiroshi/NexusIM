#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
env_file="${NEXUSIM_ENV_FILE:-${repo_root}/.env}"
compose=(docker compose --project-directory "${repo_root}" \
         --env-file "${env_file}" -f "${repo_root}/compose.yaml")

"${compose[@]}" ps
echo
echo "Persistent volumes:"
docker volume ls --filter name=nexusim-
echo
echo "Redis node subscribers:"
"${compose[@]}" exec -T redis sh -ec \
  'redis-cli --no-auth-warning -a "$REDIS_PASSWORD" PUBSUB NUMSUB nexusim:chat:node:chatserver-1 nexusim:chat:node:chatserver-2'
