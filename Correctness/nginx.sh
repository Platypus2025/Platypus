#!/usr/bin/env bash

set -e
ROOT_DIR="$PWD/.."
NGINX_BIN="${ROOT_DIR}/binaries/artifact_binaries_instrumented/nginx"

if [ ! -d "nginx-tests" ]; then
  echo "[*] Cloning nginx-tests..."
  git clone https://github.com/nginx/nginx-tests.git
fi

cd nginx-tests || exit 1

echo "[*] Using nginx binary: ${NGINX_BIN}"

export TEST_NGINX_BINARY="${NGINX_BIN}"

echo "[*] Running nginx test suite..."
prove .