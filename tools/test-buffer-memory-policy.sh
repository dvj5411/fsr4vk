#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/fsr4-memory-policy.XXXXXX")
includes=()
if [[ -n "${VULKAN_SDK:-}" ]]; then includes+=("-I$VULKAN_SDK/include"); fi
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror "${includes[@]}" \
  "$root/native/buffer-memory-policy-test.cpp" -o "$test_dir/memory-policy-test"
"$test_dir/memory-policy-test"
