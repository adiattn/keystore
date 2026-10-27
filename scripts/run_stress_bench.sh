#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

echo "Building project..."
mkdir -p "${BUILD_DIR}"
(cd "${BUILD_DIR}" && cmake .. -DCMAKE_BUILD_TYPE=Release >/dev/null && make -j"$(nproc)" >/dev/null)

echo "Executing KVStore benchmark suite..."
"${BUILD_DIR}/kvstore-benchmark"
