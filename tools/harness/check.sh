#!/usr/bin/env bash
# Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"

out=${OSKATE_HARNESS_BIN:-${TMPDIR:-/tmp}/opus-skate-harness}
mode=fast
runner_args=()

while [ $# -gt 0 ]; do
    case "$1" in
        --filter)     runner_args+=(--filter "$2"); shift 2 ;;
        --repeat)     runner_args+=(--repeat "$2"); shift 2 ;;
        --verbose)    runner_args+=(--verbose); shift ;;
        --monkey)     runner_args+=(--monkey "$2"); shift 2 ;;
        --junit)      runner_args+=(--junit "$2"); shift 2 ;;
        --tsan)       mode=tsan; shift ;;
        --asan)       mode=asan; shift ;;
        --out)        out=$2; shift 2 ;;
        --clean)      rm -f "$out"; echo "removed $out"; exit 0 ;;
        -h|--help)    echo "usage: bash tools/harness/check.sh [--filter name] [--repeat n] [--junit file] [--tsan|--asan]"; exit 0 ;;
        *)            echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

case "$mode" in
    fast) cxx_flags=(-O2) ;;
    tsan) cxx_flags=(-O1 -g -fsanitize=thread) ;;
    asan) cxx_flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer) ;;
esac

if [ "$mode" = tsan ]; then
    runner_args=(--filter audio "${runner_args[@]}")
    export TSAN_OPTIONS="halt_on_error=1 exitcode=66 second_deadlock_stack=1"
fi

echo "== building ($mode) -> $out"
g++ -std=c++17 -Itools/harness/stub -Itools/harness -I. \
    -Wall -Wextra -Wno-conversion -Wno-unused-parameter \
    "${cxx_flags[@]}" -o "$out" tools/harness/runner.cpp

echo "== running"
set +e
"$out" "${runner_args[@]}"
status=$?
set -e
exit $status
