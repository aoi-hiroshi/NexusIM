#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
binary="${1:-${repo_root}/services/user_service/bin/UserService}"
env_file="${2:-${repo_root}/deploy/config/user-service.env}"
runtime_dir="${3:-${repo_root}/run/user-service}"

if [[ ! -x "${binary}" ]]; then
    echo "UserService executable not found: ${binary}" >&2
    exit 2
fi
if [[ ! -f "${env_file}" ]]; then
    echo "Missing ${env_file}; copy user-service.env.example and insert credentials" >&2
    exit 2
fi

mkdir -p "${runtime_dir}/logs" "${runtime_dir}/pids"
pid_file="${runtime_dir}/pids/user-service.pid"
log_file="${runtime_dir}/logs/user-service.log"
if [[ -f "${pid_file}" ]]; then
    old_pid="$(<"${pid_file}")"
    if [[ "${old_pid}" =~ ^[0-9]+$ ]] && kill -0 "${old_pid}" 2>/dev/null; then
        echo "UserService is already running as PID ${old_pid}" >&2
        exit 2
    fi
fi

(
    set -a
    # shellcheck disable=SC1090
    source "${env_file}"
    set +a
    : "${MPRPC_RPC_SERVER_IP:?MPRPC_RPC_SERVER_IP is required}"
    : "${MPRPC_RPC_SERVER_PORT:?MPRPC_RPC_SERVER_PORT is required}"
    : "${MPRPC_ZOOKEEPER_IP:?MPRPC_ZOOKEEPER_IP is required}"
    : "${MPRPC_ZOOKEEPER_PORT:?MPRPC_ZOOKEEPER_PORT is required}"
    : "${CHAT_DB_NAME:?CHAT_DB_NAME is required}"
    : "${CHAT_DB_USER:?CHAT_DB_USER is required}"
    : "${CHAT_DB_PASSWORD:?CHAT_DB_PASSWORD is required}"
    if [[ "${CHAT_DB_PASSWORD}" == "replace-with-a-secret" ]]; then
        echo "UserService: replace the database password placeholder" >&2
        exit 2
    fi
    nohup "${binary}" >>"${log_file}" 2>&1 &
    echo "$!" >"${pid_file}"
)

new_pid="$(<"${pid_file}")"
sleep 1
if ! kill -0 "${new_pid}" 2>/dev/null; then
    echo "UserService failed to stay running; inspect ${log_file}" >&2
    exit 1
fi
echo "started UserService pid=${new_pid} log=${log_file}"
