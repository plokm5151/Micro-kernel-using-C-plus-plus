#!/usr/bin/env sh
set -eu

# Export all repo sources into a single bundle text file.
# Portable POSIX sh (works on macOS / Linux).

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUTPUT_PATH="${1:-${REPO_ROOT}/build/repo_sources_bundle.txt}"
mkdir -p "$(dirname "${OUTPUT_PATH}")"

# Work at repo root (avoid subshell pipeline surprises)
cd "${REPO_ROOT}"

# Compose inclusion patterns:
#  - Code:  *.c *.cc *.cpp *.h *.hpp *.s *.S *.ld
#  - Scripts: *.sh *.py
#  - Config/Docs: *.yaml *.yml *.md
#  - Named files (no extension): Makefile, Dockerfile, .gitignore, README*, LICENSE*
LIST_FILE="$(mktemp)"
trap 'rm -f "${LIST_FILE}"' EXIT

find . \
  -type f \
  \( \
     -name '*.c'    -o -name '*.cc'   -o -name '*.cpp'  -o \
     -name '*.h'    -o -name '*.hpp'  -o \
     -name '*.S'    -o -name '*.s'    -o -name '*.ld'   -o \
     -name '*.sh'   -o -name '*.py'   -o \
     -name '*.yaml' -o -name '*.yml'  -o -name '*.md'   -o \
     -name 'Makefile'    -o -name 'Dockerfile'     -o \
     -name '.gitignore'  -o -name 'README'         -o -name 'README.*'  -o \
     -name 'LICENSE'     -o -name 'LICENSE.*' \
   \) \
  ! -path './build/*' \
  ! -path './.git/*' \
| LC_ALL=C sort > "${LIST_FILE}"

# Start writing bundle
{
  printf '### Repository source bundle generated on %s\n\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  while IFS= read -r FILE; do
    # Normalize path like "./foo" -> "foo"
    REL_PATH="${FILE#./}"
    printf '===== %s =====\n' "${REL_PATH}"
    if [ -f "${REL_PATH}" ]; then
      # Use -- to guard against leading dashes in filenames (just in case)
      cat -- "${REL_PATH}"
    fi
    printf '\n\n'
  done < "${LIST_FILE}"
} > "${OUTPUT_PATH}"

COUNT="$(wc -l < "${LIST_FILE}" | tr -d ' ')"
echo "[export] Wrote ${COUNT} files to ${OUTPUT_PATH}" >&2
