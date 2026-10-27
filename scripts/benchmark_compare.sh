#!/usr/bin/env bash
set -euo pipefail

# Head-to-head comparison script: Official Redis vs KVStore using official redis-benchmark
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

REDIS_PORT=6380
KV_PORT=6381
DATA_DIR="${ROOT_DIR}/bench_live_kv"
CLIENTS=50
REQUESTS=100000

echo "Building KVStore targets..."
mkdir -p "${BUILD_DIR}"
(cd "${BUILD_DIR}" && cmake .. -DCMAKE_BUILD_TYPE=Release >/dev/null && make -j"$(nproc)" >/dev/null)

if ! command -v redis-server &>/dev/null || ! command -v redis-benchmark &>/dev/null; then
    echo "Error: redis-server or redis-benchmark not found in PATH." >&2
    exit 1
fi

cleanup() {
    echo "Cleaning up background servers..."
    redis-cli -p ${REDIS_PORT} shutdown 2>/dev/null || true
    if [[ -n "${KV_PID:-}" ]] && kill -0 "${KV_PID}" 2>/dev/null; then
        kill -9 "${KV_PID}" 2>/dev/null || true
    fi
    rm -rf "${DATA_DIR}"
}
trap cleanup EXIT

echo "Starting Redis on port ${REDIS_PORT}..."
redis-server --port ${REDIS_PORT} --daemonize yes --protected-mode no --save "" --appendonly no

echo "Starting KVStore on port ${KV_PORT}..."
rm -rf "${DATA_DIR}"
"${BUILD_DIR}/kvstore-server" -p ${KV_PORT} -d "${DATA_DIR}" --threads 16 --no-sync-wal >/dev/null 2>&1 &
KV_PID=$!
sleep 1

echo "#################################################################"
echo "  BENCHMARK: Official Redis vs KVStore (redis-benchmark -q)"
echo "  Clients: ${CLIENTS} | Total Requests: ${REQUESTS}"
echo "#################################################################"

echo -e "\n--- [1] Official Redis ---"
redis-benchmark -p ${REDIS_PORT} -t set,get -c ${CLIENTS} -n ${REQUESTS} -q

echo -e "\n--- [2] KVStore (Our Implementation) ---"
redis-benchmark -p ${KV_PORT} -t set,get -c ${CLIENTS} -n ${REQUESTS} -q

echo -e "\nBenchmark completed successfully."
