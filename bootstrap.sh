#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

if [[ -x "${ROOT_DIR}/../.venv/bin/python" ]]; then
    PYTHON_BIN="${ROOT_DIR}/../.venv/bin/python"
elif [[ -x "${ROOT_DIR}/.venv/bin/python" ]]; then
    PYTHON_BIN="${ROOT_DIR}/.venv/bin/python"
fi

"${PYTHON_BIN}" "${ROOT_DIR}/fetch_repos.py"

echo "Music bootstrap complete."
echo "Build with:"
echo "  cmake -S . -B build/desktop -DMUSIC_USE_SDL=ON"
echo "  cmake --build build/desktop -j$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
