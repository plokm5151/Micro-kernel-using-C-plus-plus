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

echo "[smoke] Building kernel (make clean && make -j)..."
make clean
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

echo "[smoke] Launching QEMU with 12s timeout..."
set +e
timeout 12s "${CMD[@]}" 2>&1 | tee "${LOG_PATH}"
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

# NEW: fail fast if any exception was logged.
if grep -qF "[EXC]" "${LOG_PATH}"; then
  echo "::error ::Exception detected in boot log; see ESR/ELR/SPSR above"
  exit 1
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

# IRQ 信標與心跳檢查
flat_log=$(tr -d $'\n' < "${LOG_PATH}")
if ! grep -qF '!' "${LOG_PATH}"; then
  echo "::error ::No IRQ entries seen ('!'): check GIC init or msr daifclr,#2"
  exit 1
fi
if ! grep -qF ':' "${LOG_PATH}"; then
  echo "::error ::IRQ seen but not timer PPI #27 (':') — wrong INTID or timer not enabled"
  exit 1
fi
if ! printf '%s' "${flat_log}" | grep -q '\.'; then
  echo "::error ::Timer PPI #27 reached but heartbeat '.' missing — check timer_irq() reload/printing"
  exit 1
fi

# RR/搶佔輸出
if ! printf '%s' "${flat_log}" | grep -q 'A.*a'; then
  echo "::error ::Missing expected scheduler evidence: A then a (interleaving allowed)"
  exit 1
fi
if ! printf '%s' "${flat_log}" | grep -q 'B.*b'; then
  echo "::error ::Missing expected scheduler evidence: B then b (interleaving allowed)"
  exit 1
fi
if ! grep -qE "ab|ba" "${LOG_PATH}"; then
  echo "::error ::Missing evidence of RR preemption interleaving a/b"
  exit 1
fi
if ! printf '%s' "${flat_log}" | grep -q 'a[^b]*a'; then
  echo "::error ::Missing critical section block of a without b"
  exit 1
fi
echo "[smoke] Scheduler output verified."

# DMA 驗收（一定要看到）
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
