#!/usr/bin/env bash
set -euo pipefail

find apps backend frontend -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 | xargs -0 -r clang-format -i
