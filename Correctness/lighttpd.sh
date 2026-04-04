#!/usr/bin/env bash

ROOT_DIR="$PWD/.."
export PATH="${ROOT_DIR}/llvm-project-uninstrumented/build/bin:$PATH"

CLANG="clang"
CLANGXX="clang++"

cd "${ROOT_DIR}/binaries/lighttpd/lighttpd-1.4.79" || exit 1

make clean > /dev/null 2>&1
make distclean > /dev/null 2>&1

echo "Please wait for compilation (logs are not printed for better clarity)..."
LIGHTTPD_STATIC=yes \
CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=lld -Wl,-z,relro,-z,now" \
./configure -C --enable-static=yes > /dev/null 2>&1

make build_static=1 -j"$(nproc)" > /dev/null 2>&1

cp "${ROOT_DIR}/binaries/artifact_binaries_instrumented/lighttpd" ./src/
echo "Instrumented binary copied successfully"

make check -j8