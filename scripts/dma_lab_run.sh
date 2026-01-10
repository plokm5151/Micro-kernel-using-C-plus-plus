#!/usr/bin/env bash
set -euo pipefail

if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then
  echo "::error ::qemu-system-aarch64 not found in PATH; install QEMU to run DMA lab"
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

BUILD_DIR="${REPO_ROOT}/build"
LOG_PATH="${BUILD_DIR}/qemu-dma-lab.log"
TRACE_LOG="${BUILD_DIR}/qemu-dma-lab-trace.log"

DMA_WINDOW_POLICY="${DMA_WINDOW_POLICY:-CACHEABLE}"
DMA_LAB_MODE="${DMA_LAB_MODE:-1}"

echo "[dma-lab] Building kernel (DMA_WINDOW_POLICY=${DMA_WINDOW_POLICY} DMA_LAB_MODE=${DMA_LAB_MODE})..."
make clean
if ! make -j \
  MEM_LAB_MODE=0 \
  SYNC_LAB_MODE=0 \
  STACK_LAB_MODE=0 \
  LOCK_LAB_MODE=0 \
  SCHED_POLICY=RR \
  DMA_WINDOW_POLICY="${DMA_WINDOW_POLICY}" \
  DMA_LAB_MODE="${DMA_LAB_MODE}"; then
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

echo "[dma-lab] Launching QEMU with 6s timeout..."
set +e
timeout 6s "${CMD[@]}" 2>&1 | tee "${LOG_PATH}"
status=${PIPESTATUS[0]}
set -e

if [[ ${status} -eq 124 ]]; then
  echo "[dma-lab] QEMU terminated after timeout (expected for halted lab)."
  status=0
fi
if [[ ${status} -ne 0 ]]; then
  echo "[dma-lab] QEMU exited with status ${status}."
  exit "${status}"
fi

if [[ -s "${TRACE_LOG}" ]] && grep -Eq '(^unimp([[:space:]:]|$)|unimp:|unimplemented|guest[_[:space:]]+error|guest_errors)' "${TRACE_LOG}"; then
  echo "::error ::QEMU produced guest_errors/unimp logs (see ${TRACE_LOG})"
  tail -n 50 "${TRACE_LOG}" || true
  exit 1
fi

if grep -qF "[EXC]" "${LOG_PATH}"; then
  echo "::error ::Exception detected in lab log; see ESR/ELR/SPSR above"
  exit 1
fi

if ! grep -qF "[mmu] enabled" "${LOG_PATH}"; then
  echo "::error ::Missing expected MMU enable message: [mmu] enabled"
  exit 1
fi
if ! grep -qF "(C=1, I=1, M=1)" "${LOG_PATH}"; then
  echo "::error ::Missing expected SCTLR bits: (C=1, I=1, M=1)"
  exit 1
fi

if ! grep -qF "[dma-lab] result PASS" "${LOG_PATH}"; then
  echo "::error ::DMA lab did not report PASS; see ${LOG_PATH}"
  tail -n 80 "${LOG_PATH}" || true
  exit 1
fi

echo "[dma-lab] All lab checks passed."
