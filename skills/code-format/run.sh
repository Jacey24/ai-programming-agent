#!/usr/bin/env bash
set -euo pipefail

TARGET="${1:-.}"
find "${TARGET}" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 | xargs -0 -r clang-format -i
