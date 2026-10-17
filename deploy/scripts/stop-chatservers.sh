#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
runtime_dir="${1:-${repo_root}/run/chatservers}"

stop_one() {
    local instance="$1"
    local pid_file="${runtime_dir}/pids/${instance}.pid"
    if [[ ! -f "${pid_file}" ]]; then
        echo "${instance}: no pid file"
        return
    fi
    local pid
    pid="$(<"${pid_file}")"
    if [[ ! "${pid}" =~ ^[0-9]+$ ]]; then
        echo "${instance}: invalid pid file ${pid_file}" >&2
        return 1
    fi
    if kill -0 "${pid}" 2>/dev/null; then
        kill -TERM "${pid}"
        for _ in {1..50}; do
            kill -0 "${pid}" 2>/dev/null || break
            sleep 0.1
        done
    fi
    if kill -0 "${pid}" 2>/dev/null; then
        echo "${instance}: PID ${pid} did not exit after SIGTERM" >&2
        return 1
    fi
    rm -f -- "${pid_file}"
    echo "stopped ${instance}"
}

stop_one chatserver-1
stop_one chatserver-2
