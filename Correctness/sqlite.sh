#!/usr/bin/env bash

ROOT_DIR="$PWD/.."
export PATH="${ROOT_DIR}/llvm-project-uninstrumented/build/bin:$PATH"

CLANG="clang"
CLANGXX="clang++"

cd "${ROOT_DIR}/binaries/sqlite/sqlite-src-3500400" || exit 1

make clean > /dev/null 2>&1
make distclean > /dev/null 2>&1

echo "Please wait for compilation (logs are not printed for better clarity)..."
CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=lld -Wl,-z,relro,-z,now" \
./configure > /dev/null 2>&1

make -j"$(nproc)" > /dev/null 2>&1

cp "${ROOT_DIR}/binaries/artifact_binaries_instrumented/sqlite3" ./
echo "Instrumented binary copied successfully"

make test -j8