#!/usr/bin/env bash
set -euo pipefail

if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then
  echo "::error ::qemu-system-aarch64 not found in PATH; install QEMU to run smoke test"
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

BUILD_DIR="${REPO_ROOT}/build"
LOG_PATH="${BUILD_DIR}/qemu-smoke.log"
TRACE_LOG="${BUILD_DIR}/qemu-trace.log"

echo "[smoke] Building kernel (make -j)..."
if ! make -j; then
  echo "::error ::Kernel build failed; see make output above"
  exit 1
fi

mkdir -p "${BUILD_DIR}"
: >"${LOG_PATH}"
: >"${TRACE_LOG}"

CMD=(
  qemu-system-aarch64
  -machine virt,gic-version=3
  -cpu cortex-a72
  -smp 1
  -m 512
  -nographic
  -serial mon:stdio
  -kernel "${BUILD_DIR}/kernel.elf"
  -d guest_errors,unimp
  -D "${TRACE_LOG}"
)

echo "[smoke] Launching QEMU with 8s timeout..."
set +e
timeout 8s "${CMD[@]}" 2>&1 | tee "${LOG_PATH}"
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

# 基本啟動訊息
required_messages=(
  "[BOOT] UART ready"
  "Timer IRQ armed @1kHz"
  "[sched] starting (coop)"
)
for message in "${required_messages[@]}"; do
  if ! grep -qF "${message}" "${LOG_PATH}"; then
    echo "::error ::Missing expected boot message: ${message}"
    exit 1
  fi
done
echo "[smoke] Boot messages OK."

# RR/搶佔輸出
flat_log=$(tr -d $'\n' < "${LOG_PATH}")
if ! printf '%s' "${flat_log}" | grep -q 'A.*a'; then
  echo "::error ::Missing expected scheduler evidence: A then a (interleaving allowed)"
  exit 1
fi
if ! printf '%s' "${flat_log}" | grep -q 'B.*b'; then
  echo "::error ::Missing expected scheduler evidence: B then b (interleaving allowed)"
  exit 1
fi
if ! printf '%s' "${flat_log}" | grep -q '\.'; then
  if grep -qF '!' "${LOG_PATH}" || grep -qF ':' "${LOG_PATH}"; then
    echo "::error ::Timer heartbeat '.' missing despite IRQ beacons (!/:)"
  else
    echo "::error ::No IRQ activity detected (missing '!' and ':' beacons)"
  fi
  exit 1
fi
if ! grep -qF '!' "${LOG_PATH}"; then
  echo "::error ::Missing IRQ entry beacon '!'"
  exit 1
fi
if ! grep -qF ':' "${LOG_PATH}"; then
  echo "::error ::Missing timer IRQ beacon ':'"
  exit 1
fi
if ! grep -qE "ab|ba" "${LOG_PATH}"; then
  echo "::error ::Missing evidence of RR preemption interleaving a/b"
  exit 1
fi
if ! grep -qE "a[^b]*a" "${LOG_PATH}"; then
  echo "::error ::Missing critical section block of a without b"
  exit 1
fi
echo "[smoke] Scheduler output verified."

# DMA 驗收
if grep -qF "[DMA FAIL]" "${LOG_PATH}"; then
  echo "::error ::DMA self-test reported failure"
  exit 1
fi
if ! grep -qF "[DMA OK]" "${LOG_PATH}"; then
  echo "::error ::Missing expected DMA success message: [DMA OK]"
  exit 1
fi
echo "[smoke] DMA OK."

echo "[smoke] All checks passed."
