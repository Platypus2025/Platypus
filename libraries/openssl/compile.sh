#!/usr/bin/env bash

set -euo pipefail

OPENSSL_VERSION="3.2.1"
TARBALL="openssl-${OPENSSL_VERSION}.tar.gz"
URL="https://www.openssl.org/source/${TARBALL}"

CLANG="/usr/bin/clang-20"
LD_LLD="/usr/bin/ld.lld-20"
DYNAMIC_LINKER="/opt/benchmark_dsos/lib/ld-linux-x86-64.so.2"

PLATYPUS_CLANG="clang"
DYNSYM_PLUGIN="/home/chagi/plt_project/llvm-hackspace/llvm_passes/FindDynSym/build/libDynsym.so"

ANNOTATED_SCRIPT="/home/chagi/plt_project/plt-hardening/callback_patching/openssl-3.2.1/annotate.sh"
LIB_FOLDER_UNINSTRUMENTED="/home/chagi/Documents/artifacts/artifact_libs_uninstrumented"

echo "Downloading OpenSSL ${OPENSSL_VERSION}..."
wget "${URL}"

echo "Extracting ${TARBALL}..."
tar -xvf "${TARBALL}"

echo "Entering source directory..."
cd "openssl-${OPENSSL_VERSION}"

echo "Running Configure..."
CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./Configure

echo "Building OpenSSL..."
make -j8

echo "Preparing library folder..."
mkdir -p "${LIB_FOLDER_UNINSTRUMENTED}"

echo "Copying libraries..."
cp libcrypto.so.3 "${LIB_FOLDER_UNINSTRUMENTED}/"
cp libssl.so.3 "${LIB_FOLDER_UNINSTRUMENTED}/"


make clean

echo "Building instrumented libcrypto.so..."
LOGFILE_PATH="$PWD/dynsym.log" \
bear -- make libcrypto.so \
  CC="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" \
  CFLAGS="-O0" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j1

echo "Building instrumented libssl.so..."
LOGFILE_PATH="$PWD/dynsym.log" \
bear -- make libssl.so \
  CC="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" \
  CFLAGS="-O0" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j1

echo "Running annotation script..."
"${ANNOTATED_SCRIPT}" annon.log ./

make clean