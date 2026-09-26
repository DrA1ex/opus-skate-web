#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$ROOT/dist"

em++ "$ROOT/skate.cpp" \
  -O3 \
  -std=c++17 \
  -sUSE_SDL=2 \
  -sMIN_WEBGL_VERSION=2 \
  -sMAX_WEBGL_VERSION=2 \
  -sGL_ENABLE_GET_PROC_ADDRESS=1 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sASYNCIFY \
  -sASSERTIONS=1 \
  -sEXPORTED_FUNCTIONS='["_main","_mobile_input","_web_resize","_web_set_mobile"]' \
  --shell-file "$ROOT/web/shell.html" \
  -o "$ROOT/dist/index.html"

echo "Built: $ROOT/dist/index.html"
