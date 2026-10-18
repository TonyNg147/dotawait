#!/usr/bin/env bash
# Runs the freshly built asynctp on one golden fixture and diffs it against
# the checked-in expectation, using the same flags as golden_tests.cpp.
# Usage: golden.sh <case> [--accept]
#   --accept  overwrite <case>.expected.cpp with the current output. Only do
#             this after reading the diff and confirming the new output is
#             the intended lowering.
set -euo pipefail

case_name="${1:?usage: golden.sh <case> [--accept]}"
accept="${2:-}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
golden="$root/transpiler/tests/golden"
input="$golden/$case_name.async.cpp"
expected="$golden/$case_name.expected.cpp"

[[ -f "$input" ]] || { echo "missing fixture: $input" >&2; exit 2; }

cmake -S "$root" -B "$root/build" >/dev/null
cmake --build "$root/build" --target asynctp >/dev/null

actual="$(mktemp)"
trap 'rm -f "$actual"' EXIT
"$root/build/transpiler/asynctp" "$input" -o "$actual" --no-line-directives

if [[ "$accept" == "--accept" ]]; then
  cp "$actual" "$expected"
  echo "wrote $expected"
elif [[ ! -f "$expected" ]]; then
  echo "no expectation yet; current output:"; cat "$actual"
  echo; echo "re-run with --accept once the output above is correct" >&2
  exit 1
elif diff -u "$expected" "$actual"; then
  echo "golden $case_name: match"
else
  exit 1
fi

if ! grep -q "\"$case_name\"" "$root/transpiler/tests/golden_tests.cpp"; then
  echo "warning: '$case_name' is not registered in golden_tests.cpp main()" >&2
fi
