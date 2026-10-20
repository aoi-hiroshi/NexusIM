#!/bin/sh
set -eu

host="${1:-127.0.0.1}"
port="${2:?port is required}"
exec nc -z -w 2 "${host}" "${port}"
