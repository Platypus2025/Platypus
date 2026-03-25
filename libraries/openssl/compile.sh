#!/usr/bin/env bash

OPENSSL_VERSION="3.2.1"
TARBALL="openssl-${OPENSSL_VERSION}.tar.gz"
URL="https://www.openssl.org/source/${TARBALL}"

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

echo "Downloading OpenSSL ${OPENSSL_VERSION}..."
wget "${URL}"

echo "Extracting ${TARBALL}..."
tar -xvf "${TARBALL}"


cd "openssl-${OPENSSL_VERSION}"

CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./Configure

bear -- make -j8

cp libcrypto.so.3 "${LIB_FOLDER_UNINSTRUMENTED}/"
cp libssl.so.3 "${LIB_FOLDER_UNINSTRUMENTED}/"

rm libcrypto*
rm libssl*

find crypto -type f -name '*.o' -print -delete
find ssl -type f -name '*.o' -print -delete


LOGFILE_PATH="$PWD/dynsym_crypto.log" \
make libcrypto.so \
  CC="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" \
  CFLAGS="-O3" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j1

LOGFILE_PATH="$PWD/dynsym_ssl.log" \
make libssl.so \
  CC="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" \
  CFLAGS="-O3" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j1


"${ANNOTATED_SCRIPT}" annon.log ./


make clean
make -j8

rm libcrypto*
rm libssl*

find crypto -type f -name '*.o' -print -delete
find ssl -type f -name '*.o' -print -delete

LOGFILE_PATH="$PWD/sym_crypto.log" make libcrypto.so \
  CC="${PLATYPUS_CLANG}" \
  CFLAGS="-O3 -g -fpass-plugin=${STRUCT_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j8

LOGFILE_PATH="$PWD/sym_ssl.log" make libssl.so \
  CC="${PLATYPUS_CLANG}" \
  CFLAGS="-O3 -g -fpass-plugin=${STRUCT_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j8


rm libcrypto*
rm libssl*

find crypto -type f -name '*.o' -print -delete
find ssl -type f -name '*.o' -print -delete


cp ../dso_callbacks_crypto.cpp "${ROOT_DIR}/dso_callbacks.cpp"
cp ../BitMasks_crypto.cpp "${ROOT_DIR}/llvm-passes/BitMasks/BitMasks.cpp"
OLD_DIR="$PWD"
cd "${ROOT_DIR}/llvm-passes/BitMasks/build"
make -j8
cd "$OLD_DIR"

export PATH="${LLVM_ROOT}/build/bin:$PATH"

cat > ../libraries_crypto.json <<EOF
{
    "${ROOT_DIR}/libraries/instrumented_libs/libc.so.6": "LIBC",
    "${ROOT_DIR}/libraries/instrumented_libs/ld-linux-x86-64.so.2": "LD"
}
EOF

PROTECT_JMP=True make libcrypto.so \
  CC=${PLATYPUS_CLANG} \
  CFLAGS="-fPIC -g \
          -include ${PWD}/../masks_crypto.h \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,--strip-debug \
           -Wl,--allow-shlib-undefined \
           -Wl,--version-script=${PWD}/../exports_crypto.map \
           -Wl,--callb_getter=${PWD}/../a_crypto.txt" \
  -j8

cp ../dso_callbacks_ssl.cpp "${ROOT_DIR}/dso_callbacks.cpp"
cp ../BitMasks_ssl.cpp "${ROOT_DIR}/llvm-passes/BitMasks/BitMasks.cpp"
OLD_DIR="$PWD"
cd "${ROOT_DIR}/llvm-passes/BitMasks/build"
make -j8
cd "$OLD_DIR"

cat > ../libraries_ssl.json <<EOF
{
    "${ROOT_DIR}/libraries/instrumented_libs/libcrypto.so.3": "CRYPTO",
    "${ROOT_DIR}/libraries/instrumented_libs/libc.so.6": "LIBC",
    "${ROOT_DIR}/libraries/instrumented_libs/ld-linux-x86-64.so.2": "LD"
}
EOF

PROTECT_JMP=True make libssl.so \
  CC=${PLATYPUS_CLANG} \
  CFLAGS="-fPIC -g \
          -include ${PWD}/../masks_ssl.h \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,--strip-debug \
           -Wl,--allow-shlib-undefined \
           -Wl,--version-script=${PWD}/../exports_ssl.map \
           -Wl,--callb_getter=${PWD}/../a_ssl.txt" \
  -j8


SCRIPT_1="${ROOT_DIR}/scripts/callback_parser.py"

touch reachable_structs

python3 "$SCRIPT_1" \
  ${PWD}/../libraries_crypto.json \
  dynsym_crypto.log \
  libcrypto.so.3 \
  0 \
  CRYPTO \
  reachable_structs \
  sym_crypto.log \
  > output_crypto.txt

cp libcrypto.so.3 ../../instrumented_libs
cp libssl.so.3 ../../instrumented_libs

python3 "$SCRIPT_1" \
  ${PWD}/../libraries_ssl.json \
  dynsym_ssl.log \
  libssl.so.3 \
  0 \
  LSSL \
  reachable_structs \
  sym_ssl.log \
  > output_ssl.txt


SCRIPT_2="${ROOT_DIR}/scripts/parse_output.py"

python3 "$SCRIPT_2" output_crypto.txt header_crypto.txt
python3 "$SCRIPT_2" output_ssl.txt header_ssl.txt