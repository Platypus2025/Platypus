#!/usr/bin/env bash

# Resolve ROOT_DIR properly (handles symlinks better)
ROOT_DIR="$PWD/.."

# Common paths
BIN_DIR="$ROOT_DIR/binaries/artifact_binaries_instrumented"
LIB_INST="$ROOT_DIR/libraries/instrumented_libs"
LIB_UNINST="$ROOT_DIR/libraries/artifact_libs_uninstrumented"

export PATH="$ROOT_DIR/llvm-project/build/bin:$PATH"

echo "===== REDIS ====="
python3 sec.py \
  "$BIN_DIR/redis-server" \
  "$ROOT_DIR/binaries/redis/redis/header.txt" \
  "$LIB_INST" \
  "$LIB_UNINST" \
  redis-server libc.so

echo
echo "===== SQLITE ====="
python3 sec.py \
  "$BIN_DIR/sqlite3" \
  "$ROOT_DIR/binaries/sqlite/sqlite-src-3500400/header_sqlite3.txt" \
  "$LIB_INST" \
  "$LIB_UNINST"

echo
echo "===== NGINX ====="
python3 sec.py \
  "$BIN_DIR/nginx" \
  "$ROOT_DIR/binaries/nginx/nginx-1.28.0/header.txt" \
  "$LIB_INST" \
  "$LIB_UNINST"