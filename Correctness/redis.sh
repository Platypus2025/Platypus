#!/usr/bin/env bash

ROOT_DIR="$PWD/.."
export PATH="${ROOT_DIR}/llvm-project-uninstrumented/build/bin:$PATH"

CLANG="clang"
CLANGXX="clang++"

cd "${ROOT_DIR}/binaries/redis/redis" || exit 1

make clean > /dev/null 2>&1
make distclean > /dev/null 2>&1

echo "Please wait for compilation (logs are not printed for better clarity)..."
make -j"$(nproc)" \
  CC="${CLANG}" \
  CXX="${CLANGXX}" \
  OPT="-O3" \
  CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
  CXXFLAGS="-fPIC -O3 -g -fcf-protection=full" \
  LDFLAGS="-fuse-ld=lld -Wl,-z,relro,-z,now" \
  MALLOC=libc \
  USE_SYSTEMD=no \
  > /dev/null 2>&1

cp "${ROOT_DIR}/binaries/artifact_binaries_instrumented/redis-server" ./src/redis-server
echo "Instrumented binary copied successfully"

LC_ALL=C LANG=C make test -j8