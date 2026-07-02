#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-demo"
PORT="${MCPS_PORT:-5555}"
MONITOR_PORT="${MCPS_MONITOR_PORT:-8080}"
TOKEN="${MCPS_TOKEN:-mission-secret}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -j

SERVER_LOG="${BUILD_DIR}/demo-server.log"
"${BUILD_DIR}/mcps_server" --host 127.0.0.1 --port "${PORT}" --monitor-port "${MONITOR_PORT}" --token "${TOKEN}" --log-file "${SERVER_LOG}" &
SERVER_PID=$!

cleanup() {
    kill "${SERVER_PID}" >/dev/null 2>&1 || true
    wait "${SERVER_PID}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

sleep 0.5

"${BUILD_DIR}/mcps_client" --host 127.0.0.1 --port "${PORT}" --token "${TOKEN}" --command GET_STATUS
"${BUILD_DIR}/mcps_client" --host 127.0.0.1 --port "${PORT}" --token "${TOKEN}" --command "SET_MODE ACTIVE"
"${BUILD_DIR}/mcps_client" --host 127.0.0.1 --port "${PORT}" --token "${TOKEN}" --stream-seconds 2

if command -v curl >/dev/null 2>&1; then
    curl -fsS "http://127.0.0.1:${MONITOR_PORT}/status"
    echo
fi

echo "Demo completed. Server log: ${SERVER_LOG}"
