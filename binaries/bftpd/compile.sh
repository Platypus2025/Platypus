#!/usr/bin/env bash

BFTPD_VERSION="6.3"
TARBALL="bftpd-${BFTPD_VERSION}.tar.gz"
URL="https://downloads.sourceforge.net/project/bftpd/bftpd/bftpd-${BFTPD_VERSION}/${TARBALL}"

CLANG="/usr/bin/clang-20"
LD_LLD="/usr/bin/ld.lld-20"
LIBRARY_PATH_WITH_INSTRUMENTED="${ROOT_DIR}/libraries/instrumented_libs"
DYNAMIC_LINKER="${LIBRARY_PATH_WITH_INSTRUMENTED}/ld-linux-x86-64.so.2"


PLATYPUS_CLANG="${ROOT_DIR}/llvm-project/build/bin/clang-20"
PLATYPUS_LLD="${ROOT_DIR}/llvm-project/build/bin/ld.lld"
DYNSYM_PLUGIN="${ROOT_DIR}/llvm-passes/FindDynSym/build/libDynsym.so"
STRUCT_PLUGIN="${ROOT_DIR}/llvm-passes/LogStructs/build/libLogstructs.so"
MASK_PLUGIN="${ROOT_DIR}/llvm-passes/BitMasks/build/libBitmask.so"

ANNOTATED_SCRIPT="${ROOT_DIR}/scripts/annotate.sh"
BIN_FOLDER_UNINSTRUMENTED="${ROOT_DIR}/binaries/artifact_binaries_uninstrumented"

BIN="bftpd"

DSO_CALLBACKS_FILE="${ROOT_DIR}/dso_callbacks.cpp"


SCRIPT_1="${ROOT_DIR}/scripts/callback_parser.py"
SCRIPT_2="${ROOT_DIR}/scripts/parse_output.py"
SCRIPT_3="${ROOT_DIR}/scripts/create_header.py"

echo "Downloading bftpd ${BFTPD_VERSION}..."
wget "${URL}"

echo "Extracting ${TARBALL}..."
tar -xvf "${TARBALL}"

cp annotate.sh "./bftpd-${BFTPD_VERSION}"

echo "Entering source directory..."
cd "bftpd"

echo "Running configure..."
CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./configure

echo "Building bftpd..."
bear -- make -j"$(nproc)"

cp bftpd "${BIN_FOLDER_UNINSTRUMENTED}/"

make clean

LOGFILE_PATH="$PWD/dynsym.log" \
make "${BIN}" \
  CC=''"${PLATYPUS_CLANG}"' -g -fPIC -fpass-plugin='"${DYNSYM_PLUGIN}"' -I. -DVERSION=\"6.3\"' \
  CFLAGS='-O3' \
  LDFLAGS='-fuse-ld=lld -Wl,--allow-shlib-undefined' \
  -j1

"${ANNOTATED_SCRIPT}" annon.log ./

make clean

LOGFILE_PATH="$PWD/sym.log" make "${BIN}" \
  CC="${PLATYPUS_CLANG}" \
  CFLAGS="-O3 -g -I. -DVERSION=\\\"6.3\\\" -fpass-plugin=${STRUCT_PLUGIN}" \
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
    "${ROOT_DIR}/libraries/instrumented_libs/libcrypt.so.1.1.0": "CRYPT",
    "${ROOT_DIR}/libraries/instrumented_libs/libc.so.6": "LIBC",
    "${ROOT_DIR}/libraries/instrumented_libs/ld-linux-x86-64.so.2": "LD"
}
EOF



PROTECT_JMP=True make "${BIN}" \
  CC="${PLATYPUS_CLANG}" \
  CFLAGS="-fPIC -g \
          -O3 \
          -fcf-protection=full \
          -I. -DVERSION=\\\"6.3\\\" \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,--disable-new-dtags,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
           -Wl,--strip-debug \
           -Wl,--allow-shlib-undefined" \
  -j8


# Ensure sym.log exists. Bftpd does not produce one but the script needs even an empty file.
[ -f sym.log ] || touch sym.log

touch reachable_structs

python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  bftpd \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt

python3 "$SCRIPT_2" output.txt header.txt bin
cat "${ROOT_DIR}/libraries/libcrypt/libxcrypt-4.4.38/header.txt" >> header.txt
cat "${ROOT_DIR}/header.txt" >> header.txt

python3 "$SCRIPT_3" header.txt placeholder1__nss_systemd_getpwnam_r placeholder2__nss_sss_getpwnam_r \
placeholder3__nss_systemd_initgroups_dyn placeholder4__nss_sss_initgroups_dyn placeholder5__nss_systemd_getspnam_r \
fallthrough libnss_sss.so.2 libnss_systemd.so.2 libcap.so.2 libm.so.6

clang -c mask.c -fcf-protection=full

rm bftpd

PROTECT_JMP=True make "${BIN}" \
  CC="${PLATYPUS_CLANG}" \
  CFLAGS="-fPIC -g \
          -O3 \
          -fcf-protection=full \
          -I. -DVERSION=\\\"6.3\\\" \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,--disable-new-dtags,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
           -Wl,--strip-debug \
           -Wl,--callb_getter=$PWD/../a.txt -Wl,--allow-shlib-undefined -Wl,--no-as-needed ${LIBRARY_PATH_WITH_INSTRUMENTED}/libnss_systemd.so.2 ${LIBRARY_PATH_WITH_INSTRUMENTED}/libnss_sss.so.2 $PWD/mask.o" \
  -j8



python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  bftpd \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt

python3 "$SCRIPT_2" output.txt header.txt bin
cat "${ROOT_DIR}/libraries/libcrypt/libxcrypt-4.4.38/header.txt" >> header.txt
cat "${ROOT_DIR}/header.txt" >> header.txt

python3 "$SCRIPT_3" header.txt placeholder1__nss_systemd_getpwnam_r placeholder2__nss_sss_getpwnam_r \
placeholder3__nss_systemd_initgroups_dyn placeholder4__nss_sss_initgroups_dyn placeholder5__nss_systemd_getspnam_r \
fallthrough libnss_sss.so.2 libnss_systemd.so.2 libcap.so.2 libm.so.6

clang -c mask.c -fcf-protection=full

rm bftpd


PROTECT_JMP=True make "${BIN}" \
  CC="${PLATYPUS_CLANG}" \
  CFLAGS="-fPIC -g \
          -O3 \
          -fcf-protection=full \
          -I. -DVERSION=\\\"6.3\\\" \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,--disable-new-dtags,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
           -Wl,--strip-debug \
           -Wl,--callb_getter=$PWD/../a.txt -Wl,--allow-shlib-undefined -Wl,--no-as-needed ${LIBRARY_PATH_WITH_INSTRUMENTED}/libnss_systemd.so.2 ${LIBRARY_PATH_WITH_INSTRUMENTED}/libnss_sss.so.2 $PWD/mask.o" \
  -j8

cp bftpd ../../artifact_binaries_instrumented