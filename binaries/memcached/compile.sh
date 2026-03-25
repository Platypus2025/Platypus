#!/usr/bin/env bash

MEMCACHED_VERSION="1.6.38"
TARBALL="memcached-${MEMCACHED_VERSION}.tar.gz"
URL="https://memcached.org/files/${TARBALL}"

CLANG="clang"
LD_LLD="lld"
LIBRARY_PATH_WITH_INSTRUMENTED="${ROOT_DIR}/libraries/instrumented_libs"
DYNAMIC_LINKER="${LIBRARY_PATH_WITH_INSTRUMENTED}/ld-linux-x86-64.so.2"

PLATYPUS_CLANG="${ROOT_DIR}/llvm-project/build/bin/clang-20"
PLATYPUS_LLD="${ROOT_DIR}/llvm-project/build/bin/ld.lld"
DYNSYM_PLUGIN="${ROOT_DIR}/llvm-passes/FindDynSym/build/libDynsym.so"
STRUCT_PLUGIN="${ROOT_DIR}/llvm-passes/LogStructs/build/libLogstructs.so"
MASK_PLUGIN="${ROOT_DIR}/llvm-passes/BitMasks/build/libBitmask.so"

ANNOTATED_SCRIPT="${ROOT_DIR}/scripts/annotate.sh"
BIN_FOLDER_UNINSTRUMENTED="${ROOT_DIR}/binaries/artifact_binaries_uninstrumented"

BIN="memcached"

DSO_CALLBACKS_FILE="${ROOT_DIR}/dso_callbacks.cpp"


SCRIPT_1="${ROOT_DIR}/scripts/callback_parser.py"
SCRIPT_2="${ROOT_DIR}/scripts/parse_output.py"
SCRIPT_3="${ROOT_DIR}/scripts/create_header.py"


echo "Downloading memcached ${MEMCACHED_VERSION}..."
wget "${URL}"

echo "Extracting ${TARBALL}..."
tar -xvf "${TARBALL}"


echo "Entering source directory..."
cd "memcached-${MEMCACHED_VERSION}"

echo "Running configure..."
CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./configure

echo "Building memcached..."
bear -- make memcached -j"$(nproc)"

cp memcached "${BIN_FOLDER_UNINSTRUMENTED}/"

make clean

LOGFILE_PATH="$PWD/dynsym.log" \
make "${BIN}" \
  CC="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" \
  CFLAGS="-O3" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j1

"${ANNOTATED_SCRIPT}" annon.log ./

make clean

LOGFILE_PATH="$PWD/sym.log" make "${BIN}" \
  CC="${PLATYPUS_CLANG}" \
  CFLAGS="-O3 -g -fpass-plugin=${STRUCT_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j8


make clean


cp ../../dso_callbacks.cpp "${ROOT_DIR}/dso_callbacks.cpp"
cp ../../BitMasks.cpp "${ROOT_DIR}/llvm-passes/BitMasks/BitMasks.cpp"
OLD_DIR="$PWD"
cd "${ROOT_DIR}/llvm-passes/BitMasks/build"
make -j8
cd "$OLD_DIR"

export PATH="${LLVM_ROOT}/build/bin:$PATH"

cat > ../libraries.json <<EOF
{
    "${ROOT_DIR}/libraries/instrumented_libs/libevent-2.1.so.7.0.1": "EVEN",
    "${ROOT_DIR}/libraries/instrumented_libs/libc.so.6": "LIBC",
    "${ROOT_DIR}/libraries/instrumented_libs/ld-linux-x86-64.so.2": "LD"
}
EOF


PROTECT_JMP=True make "${BIN}" \
  CC=${PLATYPUS_CLANG} \
  CFLAGS="-fPIC -g \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
           -Wl,--strip-debug \
           -Wl,--allow-shlib-undefined" \
  -j8


touch reachable_structs


python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  memcached \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt

python3 "$SCRIPT_2" output.txt header.txt bin
cat "${ROOT_DIR}/libraries/libevent/libevent-2.1.12-stable/header.txt" >> header.txt
cat "${ROOT_DIR}/header.txt" >> header.txt

python3 "$SCRIPT_3" header.txt

clang -c mask.c -fcf-protection=full

rm memcached

PROTECT_JMP=True make "${BIN}" \
  CC=${PLATYPUS_CLANG} \
  CFLAGS="-fPIC -g \
          -include ${PWD}/masks.h \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
           -Wl,--strip-debug \
           -Wl,--callb_getter=$PWD/../a.txt -Wl,--allow-shlib-undefined $PWD/mask.o" \
  -j8


python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  memcached \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt


python3 "$SCRIPT_2" output.txt header.txt bin
cat "${ROOT_DIR}/libraries/libevent/libevent-2.1.12-stable/header.txt" >> header.txt
cat "${ROOT_DIR}/header.txt" >> header.txt

python3 "$SCRIPT_3" header.txt

clang -c mask.c -fcf-protection=full

rm memcached

PROTECT_JMP=True make "${BIN}" \
  CC=${PLATYPUS_CLANG} \
  CFLAGS="-fPIC -g \
          -include ${PWD}/masks.h \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
           -Wl,--strip-debug \
           -Wl,--callb_getter=$PWD/../a.txt -Wl,--allow-shlib-undefined $PWD/mask.o" \
  -j8

cp memcached ../../artifact_binaries_instrumented