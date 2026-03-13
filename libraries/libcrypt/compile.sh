#!/usr/bin/env bash

LIBXCRYPT_VERSION="4.4.38"
TARBALL="libxcrypt-${LIBXCRYPT_VERSION}.tar.xz"
URL="https://github.com/besser82/libxcrypt/releases/download/v${LIBXCRYPT_VERSION}/${TARBALL}"

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

echo "Downloading libxcrypt ${LIBXCRYPT_VERSION}..."
wget "${URL}"

echo "Extracting ${TARBALL}..."
tar -xvf "${TARBALL}"


echo "Entering source directory..."
cd "libxcrypt-${LIBXCRYPT_VERSION}"

CC="${CLANG} -fPIC -O3 -g -fcf-protection=full -fuse-ld=lld" \
LDFLAGS="-fuse-ld=lld -Wl,-z,relro,-z,now" \
./configure --enable-shared --disable-static

echo "Building libxcrypt..."
bear -- make -j"$(nproc)"

cp .libs/libcrypt.so.1.1.0 "${LIB_FOLDER_UNINSTRUMENTED}/"

make clean


LOGFILE_PATH="$PWD/dynsym.log" \
make CC="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" \
  CFLAGS="-O3" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j1

# echo "Running from directory: $PWD"
# echo "${ANNOTATED_SCRIPT} annon.log ./openssl-3.2.1"
"${ANNOTATED_SCRIPT}" annon.log ./


make clean


LOGFILE_PATH="$PWD/sym.log" make libcrypt.la CC="${PLATYPUS_CLANG}" \
  CFLAGS="-O3 -g -fpass-plugin=${STRUCT_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j8


make clean
make distclean

export PATH="${LLVM_ROOT}/build/bin:$PATH"

CC="clang -fPIC -O3 -g -fcf-protection=full -fuse-ld=lld" \
LDFLAGS="-fuse-ld=lld -Wl,-z,relro,-z,now" \
./configure --enable-shared --disable-static


MAP_FILE="./lib/libcrypt.map.in"
NEW_SYMBOL="CRYPT"
NEW_VERSION="XCRYPT_4.4"

if [ ! -f "$MAP_FILE" ]; then
    echo "Error: $MAP_FILE not found"
    exit 1
fi

if ! awk '{print $1}' "$MAP_FILE" | grep -qx "$NEW_SYMBOL"; then
    awk -v sym="$NEW_SYMBOL" -v ver="$NEW_VERSION" '
        BEGIN { inserted=0 }
        /^# Interfaces for code compatibility with libxcrypt v3\.1\.1 and earlier\./ && !inserted {
            print sym "                   " ver
            print ""
            inserted=1
        }
        { print }
    ' "$MAP_FILE" > "${MAP_FILE}.tmp" && mv "${MAP_FILE}.tmp" "$MAP_FILE"
fi


cp ../dso_callbacks.cpp "${ROOT_DIR}/dso_callbacks.cpp"
cp ../BitMasks.cpp "${ROOT_DIR}/llvm-passes/BitMasks/BitMasks.cpp"
OLD_DIR="$PWD"
cd "${ROOT_DIR}/llvm-passes/BitMasks/build"
make -j8
cd "$OLD_DIR"

cat > ../libraries.json <<EOF
{
    "${ROOT_DIR}/libraries/instrumented_libs/libc.so.6": "LIBC",
    "${ROOT_DIR}/libraries/instrumented_libs/ld-linux-x86-64.so.2": "LD"
}
EOF


PROTECT_JMP=True make libcrypt.la CC=${PLATYPUS_CLANG} \
  CFLAGS="-fPIC -g \
          -include ${PWD}/../masks.h \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,--strip-debug \
           -Wl,--allow-shlib-undefined" \
  -j8


SCRIPT_1="${ROOT_DIR}/scripts/callback_parser.py"

touch reachable_structs

# Ensure sym.log exists, libcrypt does not need one, but we have to provide even an empty for the following script
[ -f sym.log ] || touch sym.log


python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  .libs/libcrypt.so.1.1.0 \
  0 \
  CRYPT \
  reachable_structs \
  sym.log \
  > output.txt

cp .libs/libcrypt.so.1.1.0 ../../instrumented_libs
ln -sf libcrypt.so.1.1.0 ../../instrumented_libs/libcrypt.so.1

SCRIPT_2="${ROOT_DIR}/scripts/parse_output.py"

python3 "$SCRIPT_2" output.txt header.txt