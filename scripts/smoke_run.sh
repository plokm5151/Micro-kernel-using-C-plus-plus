#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

BUILD_DIR="${REPO_ROOT}/build"
LOG_PATH="${BUILD_DIR}/qemu-smoke.log"

mkdir -p "${BUILD_DIR}"
: >"${LOG_PATH}"

CMD=(
  qemu-system-aarch64
  -machine virt,gic-version=3
  -cpu cortex-a72
  -smp 1
  -m 512
  -nographic
  -serial mon:stdio
  -kernel "${BUILD_DIR}/kernel.elf"
)

echo "[smoke] Launching QEMU with 5s timeout..."
set +e
# Capture output for log and console simultaneously.
timeout 5s "${CMD[@]}" 2>&1 | tee "${LOG_PATH}"
status=${PIPESTATUS[0]}
set -e

if [[ ${status} -eq 124 ]]; then
  echo "[smoke] QEMU terminated after timeout (expected for idle kernel)."
  status=0
fi

if [[ ${status} -ne 0 ]]; then
  echo "[smoke] QEMU exited with status ${status}."
  exit "${status}"
fi

required_messages=(
  "[BOOT] UART ready"
  "Timer IRQ armed @1kHz"
)

for message in "${required_messages[@]}"; do
  if ! grep -qF "${message}" "${LOG_PATH}"; then
    echo "::error ::Missing expected boot message: ${message}"
    exit 1
  fi
done

echo "[smoke] All expected boot messages found."
