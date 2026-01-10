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
SMOKE_MAKE_ARGS=(
  DMA_LAB_MODE=0
  MEM_LAB_MODE=0
  SYNC_LAB_MODE=0
  STACK_LAB_MODE=0
  LOCK_LAB_MODE=0
  SCHED_POLICY=RR
)
if ! make -j "${SMOKE_MAKE_ARGS[@]}"; then
  echo "::error ::Kernel build failed; see make output above"
  exit 1
fi

mkdir -p "${BUILD_DIR}"

# Static checks (fast, deterministic).
echo "[smoke] Verifying dma_* barriers use OSH..."
CLANG=""
for c in clang-20 clang-19 clang-18 clang-17 clang-16 clang-15 clang-14 clang; do
  if command -v "${c}" >/dev/null 2>&1; then
    CLANG="${c}"
    break
  fi
done
if [[ -z "${CLANG}" ]]; then
  echo "::error ::clang not found in PATH; cannot run static barrier verification"
  exit 2
fi

DMA_CHECK_C="${BUILD_DIR}/__dma_barrier_check.c"
DMA_CHECK_S="${BUILD_DIR}/__dma_barrier_check.S"
cat >"${DMA_CHECK_C}" <<'EOF'
#include "arch/barrier.h"
__attribute__((used)) void __verify_dma_barriers(void) {
  dma_wmb();
  dma_rmb();
  dma_mb();
}
EOF
if ! "${CLANG}" -target aarch64-unknown-none -ffreestanding -O2 -S -Iinclude -Isrc "${DMA_CHECK_C}" -o "${DMA_CHECK_S}"; then
  echo "::error ::Failed to compile DMA barrier check to assembly"
  exit 1
fi
if ! grep -qE '^[[:space:]]*dmb[[:space:]]+oshst([[:space:]]|$)' "${DMA_CHECK_S}"; then
  echo "::error ::dma_wmb() did not lower to 'dmb oshst'"
  exit 1
fi
if ! grep -qE '^[[:space:]]*dmb[[:space:]]+oshld([[:space:]]|$)' "${DMA_CHECK_S}"; then
  echo "::error ::dma_rmb() did not lower to 'dmb oshld'"
  exit 1
fi
if ! grep -qE '^[[:space:]]*dmb[[:space:]]+osh([[:space:]]|$)' "${DMA_CHECK_S}"; then
  echo "::error ::dma_mb() did not lower to 'dmb osh'"
  exit 1
fi

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

# Fail fast if QEMU logged guest_errors/unimp output.
if [[ -s "${TRACE_LOG}" ]] && grep -Eq '(^unimp([[:space:]:]|$)|unimp:|unimplemented|guest[_[:space:]]+error|guest_errors)' "${TRACE_LOG}"; then
  echo "::error ::QEMU produced guest_errors/unimp logs (see ${TRACE_LOG})"
  tail -n 50 "${TRACE_LOG}" || true
  exit 1
fi

# NEW: fail fast if any exception was logged.
if grep -qF "[EXC]" "${LOG_PATH}"; then
  echo "::error ::Exception detected in boot log; see ESR/ELR/SPSR above"
  exit 1
fi

# Basic boot messages
required_messages=(
  "[BOOT] UART ready"
  "[mmu] enabled"
  "(C=1, I=1, M=1)"
  "Timer IRQ armed @1kHz"
  "[sched] starting (coop)"
)
for message in "${required_messages[@]}"; do
  if ! grep -qF "${message}" "${LOG_PATH}"; then
    echo "::error ::Missing expected boot message: ${message}"
    exit 1
  fi
done

if ! grep -Eq '\[dma-policy\][[:space:]]+(CACHEABLE|NONCACHEABLE)([[:space:]]|$)' "${LOG_PATH}"; then
  echo "::error ::Missing expected DMA policy line: [dma-policy] CACHEABLE|NONCACHEABLE"
  exit 1
fi
echo "[smoke] Boot messages OK."

# IRQ beacon and heartbeat checks
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

# RR / preemption output
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

# DMA validation (must see)
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
