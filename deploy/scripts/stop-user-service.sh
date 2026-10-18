#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
runtime_dir="${1:-${repo_root}/run/user-service}"
pid_file="${runtime_dir}/pids/user-service.pid"

if [[ ! -f "${pid_file}" ]]; then
    echo "UserService is not running (no pid file)"
    exit 0
fi
pid="$(<"${pid_file}")"
if [[ "${pid}" =~ ^[0-9]+$ ]] && kill -0 "${pid}" 2>/dev/null; then
    kill "${pid}"
    for _ in {1..20}; do
        if ! kill -0 "${pid}" 2>/dev/null; then break; fi
        sleep 0.1
    done
fi
rm -f -- "${pid_file}"
echo "stopped UserService"
