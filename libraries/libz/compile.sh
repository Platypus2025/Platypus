#!/usr/bin/env bash

ZLIB_VERSION="1.3.1"
TARBALL="zlib-${ZLIB_VERSION}.tar.gz"
URL="https://zlib.net/fossils/${TARBALL}"

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

echo "Downloading zlib ${ZLIB_VERSION}..."
wget "${URL}"

echo "Extracting ${TARBALL}..."
tar -xvf "${TARBALL}"


echo "Entering source directory..."
cd "zlib-${ZLIB_VERSION}"

CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./configure

echo "Building zlib..."
bear -- make -j"$(nproc)"


cp libz.so.1.3.1 "${LIB_FOLDER_UNINSTRUMENTED}/"

make clean


LOGFILE_PATH="$PWD/dynsym.log" \
make libz.so.1.3.1 \
  CC="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" \
  CFLAGS="-O3" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j1


"${ANNOTATED_SCRIPT}" annon.log ./


make clean


LOGFILE_PATH="$PWD/sym.log" make libz.so.1.3.1  \
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


PROTECT_JMP=True make libz.so.1.3.1 \
  CC=${PLATYPUS_CLANG} \
  LDSHARED="${PLATYPUS_CLANG} -shared -Wl,--version-script,${PWD}/../libz.map" \
  CFLAGS="-fPIC -g \
          -include ${PWD}/../masks.h \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  SFLAGS="-fPIC -g \
          -include ${PWD}/../masks.h \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld \
           -Wl,-z,relro,-z,now \
           -Wl,--strip-debug \
           -Wl,--allow-shlib-undefined \
           -Wl,--undefined-version" \
  -j8


SCRIPT_1="${ROOT_DIR}/scripts/callback_parser.py"

touch reachable_structs


mv libz.so libz.so.disabled
mv libz.so.1 libz.so.1.disabled

python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  libz.so.1.3.1 \
  0 \
  LIBZ \
  reachable_structs \
  sym.log \
  > output.txt

cp libz.so.1.3.1 ../../instrumented_libs

SCRIPT_2="${ROOT_DIR}/scripts/parse_output.py"

python3 "$SCRIPT_2" output.txt header.txt

mv libz.so.1.disabled libz.so.1
mv libz.so.disabled libz.so