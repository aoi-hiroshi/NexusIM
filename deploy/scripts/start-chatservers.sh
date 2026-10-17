#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
binary="${1:-${repo_root}/chatserver/bin/ChatServer}"
config_dir="${2:-${repo_root}/deploy/config}"
runtime_dir="${3:-${repo_root}/run/chatservers}"

if [[ ! -x "${binary}" ]]; then
    echo "ChatServer executable not found: ${binary}" >&2
    exit 2
fi

read_setting() {
    local instance="$1"
    local setting_name="$2"
    local env_file="${config_dir}/${instance}.env"
    if [[ ! -f "${env_file}" ]]; then
        echo "Missing ${env_file}; copy the matching .env.example and insert secrets" >&2
        exit 2
    fi
    (
        set -a
        # shellcheck disable=SC1090
        source "${env_file}"
        set +a
        printf '%s' "${!setting_name-}"
    )
}

node_1="$(read_setting chatserver-1 CHAT_NODE_ID)"
node_2="$(read_setting chatserver-2 CHAT_NODE_ID)"
port_1="$(read_setting chatserver-1 CHAT_LISTEN_PORT)"
port_2="$(read_setting chatserver-2 CHAT_LISTEN_PORT)"
redis_host_1="$(read_setting chatserver-1 CHAT_REDIS_HOST)"
redis_host_2="$(read_setting chatserver-2 CHAT_REDIS_HOST)"
redis_port_1="$(read_setting chatserver-1 CHAT_REDIS_PORT)"
redis_port_2="$(read_setting chatserver-2 CHAT_REDIS_PORT)"

if [[ -z "${node_1}" || "${node_1}" == "${node_2}" ]]; then
    echo "ChatServer instances require distinct non-empty CHAT_NODE_ID values" >&2
    exit 2
fi
if [[ ! "${node_1}" =~ ^[A-Za-z0-9._-]{1,64}$ ||
      ! "${node_2}" =~ ^[A-Za-z0-9._-]{1,64}$ ]]; then
    echo "CHAT_NODE_ID contains unsupported characters or is longer than 64 bytes" >&2
    exit 2
fi
if [[ -z "${port_1}" || "${port_1}" == "${port_2}" ]]; then
    echo "ChatServer instances require distinct non-empty CHAT_LISTEN_PORT values" >&2
    exit 2
fi
if [[ "${redis_host_1}:${redis_port_1}" != "${redis_host_2}:${redis_port_2}" ]]; then
    echo "Both ChatServer instances must use the same Redis Presence namespace" >&2
    exit 2
fi

mkdir -p "${runtime_dir}/logs" "${runtime_dir}/pids"

start_one() {
    local instance="$1"
    local env_file="${config_dir}/${instance}.env"
    local pid_file="${runtime_dir}/pids/${instance}.pid"
    local log_file="${runtime_dir}/logs/${instance}.log"

    if [[ ! -f "${env_file}" ]]; then
        echo "Missing ${env_file}; copy the matching .env.example and insert secrets" >&2
        exit 2
    fi
    if [[ -f "${pid_file}" ]]; then
        local old_pid
        old_pid="$(<"${pid_file}")"
        if [[ "${old_pid}" =~ ^[0-9]+$ ]] && kill -0 "${old_pid}" 2>/dev/null; then
            echo "${instance} is already running as PID ${old_pid}" >&2
            exit 2
        fi
    fi

    (
        set -a
        # shellcheck disable=SC1090
        source "${env_file}"
        set +a
        : "${CHAT_NODE_ID:?CHAT_NODE_ID is required}"
        : "${CHAT_LISTEN_PORT:?CHAT_LISTEN_PORT is required}"
        : "${CHAT_REDIS_HOST:?CHAT_REDIS_HOST is required}"
        : "${CHAT_REDIS_PORT:?CHAT_REDIS_PORT is required}"
        : "${CHAT_DB_HOST:?CHAT_DB_HOST is required}"
        : "${CHAT_DB_NAME:?CHAT_DB_NAME is required}"
        : "${CHAT_DB_USER:?CHAT_DB_USER is required}"
        : "${CHAT_DB_PASSWORD:?CHAT_DB_PASSWORD is required}"
        if [[ "${CHAT_DB_PASSWORD}" == "replace-with-a-secret" ]]; then
            echo "${instance}: replace the database password placeholder" >&2
            exit 2
        fi
        nohup "${binary}" >>"${log_file}" 2>&1 &
        echo "$!" >"${pid_file}"
    )

    local new_pid
    new_pid="$(<"${pid_file}")"
    sleep 1
    if ! kill -0 "${new_pid}" 2>/dev/null; then
        echo "${instance} failed to stay running; inspect ${log_file}" >&2
        exit 1
    fi
    echo "started ${instance} pid=${new_pid} log=${log_file}"
}

start_one chatserver-1
start_one chatserver-2
