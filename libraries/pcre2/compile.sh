#!/usr/bin/env bash

PCRE2_VERSION="10.43"
TARBALL="pcre2-${PCRE2_VERSION}.tar.gz"
URL="https://github.com/PCRE2Project/pcre2/releases/download/pcre2-${PCRE2_VERSION}/${TARBALL}"

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

echo "Downloading PCRE2 ${PCRE2_VERSION}..."
wget "${URL}"

echo "Extracting ${TARBALL}..."
tar -xvf "${TARBALL}"


cd "pcre2-${PCRE2_VERSION}"

CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./configure --enable-shared --disable-static

echo "Building PCRE2..."
bear -- make -j"$(nproc)"

# echo "Copying libraries..."
cp .libs/libpcre2-8.so.0.12.0 "${LIB_FOLDER_UNINSTRUMENTED}/"

make clean


echo "Building instrumented libcrypto.so..."
LOGFILE_PATH="$PWD/dynsym.log" \
make libpcre2-8.la \
  CC="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" \
  CFLAGS="-O3" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j1

# echo "Running from directory: $PWD"
# echo "${ANNOTATED_SCRIPT} annon.log ./openssl-3.2.1"
"${ANNOTATED_SCRIPT}" annon.log ./


make clean


LOGFILE_PATH="$PWD/sym.log" make libpcre2-8.la \
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

PROTECT_JMP=True make libpcre2-8.la \
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
  .libs/libpcre2-8.so.0.12.0 \
  0 \
  PCR \
  reachable_structs \
  sym.log \
  > output.txt

cp .libs/libpcre2-8.so.0.12.0 ../../instrumented_libs
ln -sf libpcre2-8.so.0.12.0 ../../instrumented_libs/libpcre2-8.so.0

SCRIPT_2="${ROOT_DIR}/scripts/parse_output.py"

python3 "$SCRIPT_2" output.txt header.txt
