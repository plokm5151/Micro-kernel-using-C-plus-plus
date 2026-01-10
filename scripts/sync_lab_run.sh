#!/usr/bin/env bash
set -euo pipefail

if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then
  echo "::error ::qemu-system-aarch64 not found in PATH; install QEMU to run sync labs"
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

BUILD_DIR="${REPO_ROOT}/build"
LOG_PATH="${BUILD_DIR}/qemu-sync-lab.log"
TRACE_LOG="${BUILD_DIR}/qemu-sync-lab-trace.log"

SYNC_LAB_MODE="${SYNC_LAB_MODE:-1}"

echo "[sync-lab] Building kernel (SCHED_POLICY=PRIO SYNC_LAB_MODE=${SYNC_LAB_MODE})..."
make clean
if ! make -j \
  DMA_LAB_MODE=0 \
  MEM_LAB_MODE=0 \
  STACK_LAB_MODE=0 \
  LOCK_LAB_MODE=0 \
  SCHED_POLICY=PRIO \
  SYNC_LAB_MODE="${SYNC_LAB_MODE}"; then
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

echo "[sync-lab] Launching QEMU with 10s timeout..."
set +e
timeout 10s "${CMD[@]}" 2>&1 | tee "${LOG_PATH}"
status=${PIPESTATUS[0]}
set -e

if [[ ${status} -eq 124 ]]; then
  echo "[sync-lab] QEMU terminated after timeout (expected for lab)."
  status=0
fi
if [[ ${status} -ne 0 ]]; then
  echo "[sync-lab] QEMU exited with status ${status}."
  exit "${status}"
fi

if [[ -s "${TRACE_LOG}" ]] && grep -Eq '(^unimp([[:space:]:]|$)|unimp:|unimplemented|guest[_[:space:]]+error|guest_errors)' "${TRACE_LOG}"; then
  echo "::error ::QEMU produced guest_errors/unimp logs (see ${TRACE_LOG})"
  tail -n 50 "${TRACE_LOG}" || true
  exit 1
fi

if grep -qF "[EXC]" "${LOG_PATH}"; then
  echo "::error ::Unexpected exception in sync lab log; see ${LOG_PATH}"
  exit 1
fi

case "${SYNC_LAB_MODE}" in
  1)
    required=("inversion observed; enabling PI" "result PASS")
    ;;
  2)
    required=("[deadlock-lab] deadlock observed")
    ;;
  3|4)
    required=("[deadlock-lab] result PASS")
    ;;
  5)
    required=("[lockdep] result PASS")
    ;;
  *)
    echo "::error ::Unknown SYNC_LAB_MODE=${SYNC_LAB_MODE} for script expectations"
    exit 2
    ;;
esac

for needle in "${required[@]}"; do
  if ! grep -qF "${needle}" "${LOG_PATH}"; then
    echo "::error ::Missing expected sync lab output: ${needle}"
    tail -n 160 "${LOG_PATH}" || true
    exit 1
  fi
done

echo "[sync-lab] All lab checks passed."
