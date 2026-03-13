#!/usr/bin/env bash

LIBEVENT_VERSION="2.1.12-stable"
TARBALL="libevent-${LIBEVENT_VERSION}.tar.gz"
URL="https://github.com/libevent/libevent/releases/download/release-${LIBEVENT_VERSION}/${TARBALL}"

CLANG="/usr/bin/clang-20"
LD_LLD="/usr/bin/ld.lld-20"
DYNAMIC_LINKER="${ROOT_DIR}/libraries/instrumented_libs/ld-linux-x86-64.so.2"

PLATYPUS_CLANG="${ROOT_DIR}/llvm-project/build/bin/clang-20"
PLATYPUS_LLD="${ROOT_DIR}/llvm-project/build/bin/ld.lld"
DYNSYM_PLUGIN="${ROOT_DIR}/llvm-passes/FindDynSym/build/libDynsym.so"
STRUCT_PLUGIN="${ROOT_DIR}/llvm-passes/LogStructs/build/libLogstructs.so"
MASK_PLUGIN="${ROOT_DIR}/llvm-passes/BitMasks/build/libBitmask.so"

ANNOTATED_SCRIPT="${ROOT_DIR}/scripts/annotate.sh"
LIB_FOLDER_UNINSTRUMENTED="${ROOT_DIR}/libraries/artifact_libs_uninstrumented"

DSO_CALLBACKS_FILE="${ROOT_DIR}/dso_callbacks.cpp"

echo "Downloading libevent ${LIBEVENT_VERSION}..."
wget "${URL}"

echo "Extracting ${TARBALL}..."
tar -xvf "${TARBALL}"


echo "Entering source directory..."
cd "libevent-${LIBEVENT_VERSION}"

echo "Running configure..."
CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./configure --enable-shared --disable-static

echo "Building libevent..."
bear -- make -j"$(nproc)"


cp .libs/libevent-2.1.so.7.0.1 "${LIB_FOLDER_UNINSTRUMENTED}/"

make clean


LOGFILE_PATH="$PWD/dynsym.log" \
make libevent.la \
  CC="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" \
  CFLAGS="-O3" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j1

# echo "Running from directory: $PWD"
# echo "${ANNOTATED_SCRIPT} annon.log ./openssl-3.2.1"
"${ANNOTATED_SCRIPT}" annon.log ./


make clean


LOGFILE_PATH="$PWD/sym.log" make libevent.la \
  CC="${PLATYPUS_CLANG}" \
  CFLAGS="-O3 -g -fpass-plugin=${STRUCT_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j8


make clean


cp ../dso_callbacks.cpp "${ROOT_DIR}/dso_callbacks.cpp"
cp ../BitMasks.cpp "${ROOT_DIR}/llvm-passes/BitMasks/BitMasks.cpp"
OLD_DIR="$PWD"
cd "${ROOT_DIR}/llvm-passes/BitMasks/build"
make -j8
cd "$OLD_DIR"

export PATH="${LLVM_ROOT}/build/bin:$PATH"

cat > ../libraries.json <<EOF
{
    "${ROOT_DIR}/libraries/instrumented_libs/libc.so.6": "LIBC",
    "${ROOT_DIR}/libraries/instrumented_libs/ld-linux-x86-64.so.2": "LD"
}
EOF


PROTECT_JMP=True make libevent.la \
  CC=${PLATYPUS_CLANG} \
  CFLAGS="-fPIC -g \
          -include ${PWD}/../masks.h \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=${PLATYPUS_LLD} \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,--strip-debug \
           -Wl,--allow-shlib-undefined" \
  -j8


SCRIPT_1="${ROOT_DIR}/scripts/callback_parser.py"

touch reachable_structs


python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  .libs/libevent-2.1.so.7.0.1 \
  0 \
  EVEN \
  reachable_structs \
  sym.log \
  > output.txt

cp .libs/libevent-2.1.so.7.0.1 ../../instrumented_libs

SCRIPT_2="${ROOT_DIR}/scripts/parse_output.py"

python3 "$SCRIPT_2" output.txt header.txt
