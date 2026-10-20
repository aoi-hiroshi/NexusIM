#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
binary="${1:-${repo_root}/services/ai_service/bin/AIService}"
env_file="${2:-${repo_root}/deploy/config/ai-service.env}"
runtime_dir="${3:-${repo_root}/run/ai-service}"

if [[ ! -x "${binary}" ]]; then
    echo "AIService executable not found: ${binary}" >&2
    exit 2
fi
if [[ ! -f "${env_file}" ]]; then
    echo "Missing ${env_file}; copy ai-service.env.example and set runtime values" >&2
    exit 2
fi

mkdir -p "${runtime_dir}/logs" "${runtime_dir}/pids"
pid_file="${runtime_dir}/pids/ai-service.pid"
log_file="${runtime_dir}/logs/ai-service.log"
if [[ -f "${pid_file}" ]]; then
    old_pid="$(<"${pid_file}")"
    if [[ "${old_pid}" =~ ^[0-9]+$ ]] && kill -0 "${old_pid}" 2>/dev/null; then
        echo "AIService is already running as PID ${old_pid}" >&2
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
    : "${DEEPSEEK_BASE_URL:?DEEPSEEK_BASE_URL is required}"
    : "${DEEPSEEK_MODEL:?DEEPSEEK_MODEL is required}"
    # An empty key is allowed at process startup so ChatServer can return the
    # explicit AI_CONFIG_MISSING business error while ordinary chat stays live.
    nohup "${binary}" >>"${log_file}" 2>&1 &
    echo "$!" >"${pid_file}"
)

new_pid="$(<"${pid_file}")"
rpc_ip="$(set -a; source "${env_file}"; set +a; printf '%s' "${MPRPC_RPC_SERVER_IP}")"
rpc_port="$(set -a; source "${env_file}"; set +a; printf '%s' "${MPRPC_RPC_SERVER_PORT}")"
probe_host="${rpc_ip}"
if [[ "${probe_host}" == "0.0.0.0" ]]; then probe_host="127.0.0.1"; fi

ready=false
for _ in {1..250}; do
    if ! kill -0 "${new_pid}" 2>/dev/null; then
        rm -f -- "${pid_file}"
        echo "AIService exited during startup; inspect ${log_file}" >&2
        exit 1
    fi
    if (exec 3<>"/dev/tcp/${probe_host}/${rpc_port}") 2>/dev/null; then
        ready=true
        break
    fi
    sleep 0.1
done
if [[ "${ready}" != true ]]; then
    kill "${new_pid}" 2>/dev/null || true
    rm -f -- "${pid_file}"
    echo "AIService did not listen on ${rpc_ip}:${rpc_port}; inspect ${log_file}" >&2
    exit 1
fi
echo "started AIService pid=${new_pid} log=${log_file}"
