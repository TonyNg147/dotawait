#!/usr/bin/env bash
# One entry point for every build/test check in this repo, so agents and
# humans run exactly the same thing. Usage: verify.sh [quick|full|asan|all]
set -euo pipefail

mode="${1:-quick}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$root"
jobs="$(nproc 2>/dev/null || echo 4)"

step() { printf '\n==> %s\n' "$*"; }

cmake_suite() { # <build-dir> <extra ctest args...> [-- <extra cmake args...>]
  local dir="$1"; shift
  local ctest_args=() cmake_args=()
  while [[ $# -gt 0 && "$1" != "--" ]]; do ctest_args+=("$1"); shift; done
  [[ $# -gt 0 ]] && shift && cmake_args=("$@")
  step "configure $dir"
  cmake -S . -B "$dir" -DCMAKE_BUILD_TYPE=Debug "${cmake_args[@]}" >/dev/null
  step "build $dir"
  cmake --build "$dir" --parallel "$jobs"
  step "ctest $dir ${ctest_args[*]}"
  ctest --test-dir "$dir" --output-on-failure "${ctest_args[@]}"
}

quick() { cmake_suite build -E consumer_example; }
full() {
  cmake_suite build
  step "Makefile parity (build-make/)"
  make --no-print-directory BUILD=build-make test
}
asan() {
  # consumer_example re-configures a nested project without these flags, so
  # it adds nothing here; the plain build already covers it.
  ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    cmake_suite build-asan -E consumer_example -- \
    "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer"
}

case "$mode" in
  quick) quick ;;
  full) full ;;
  asan) asan ;;
  all) full; asan ;;
  *) echo "usage: $0 [quick|full|asan|all]" >&2; exit 2 ;;
esac
step "verify $mode: OK"
