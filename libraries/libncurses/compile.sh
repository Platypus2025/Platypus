#!/usr/bin/env bash

NCURSES_VERSION="6.3"
TARBALL="ncurses-${NCURSES_VERSION}.tar.gz"
URL="https://ftp.gnu.org/gnu/ncurses/${TARBALL}"

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

echo "Downloading ncurses ${NCURSES_VERSION}..."
wget "${URL}"

echo "Extracting ${TARBALL}..."
tar -xvf "${TARBALL}"


echo "Entering source directory..."
cd "ncurses-${NCURSES_VERSION}"

echo "Running configure..."
CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./configure \
  --with-shared \
  --without-normal \
  --without-debug \
  --with-termlib \
  --without-tests \
  --without-cxx-binding \
  --without-cxx \
  --without-ada


echo "Building ncurses..."
bear -- make -j"$(nproc)"

cp ./lib/libncurses.so.6.3 "${LIB_FOLDER_UNINSTRUMENTED}/"


make clean
make distclean

SCRIPT_1="${ROOT_DIR}/scripts/callback_parser.py"
SCRIPT_2="${ROOT_DIR}/scripts/parse_output.py"

export PATH="${LLVM_ROOT}/build/bin:$PATH"

CC="clang -fPIC -O3 -g -fcf-protection=full -fuse-ld=lld" \
LDFLAGS="-fuse-ld=lld -Wl,-z,relro,-z,now" \
./configure \
  --with-shared \
  --without-normal \
  --without-debug \
  --with-termlib \
  --without-tests \
  --without-cxx-binding \
  --without-cxx \
  --without-ada

make libs -j8

touch reachable_structs


rm -f lib/libncurses.so*
rm -f obj_s/lib_*.o
rm -f obj_s/hardscroll.o obj_s/hashmap.o obj_s/tty_update.o obj_s/lib_mvcur.o

LOGFILE_PATH="$PWD/dynsym_ncurses.log" \
make libs CC="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" \
  CFLAGS="-O3" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j1

rm -f lib/libncurses.so*
rm -f obj_s/lib_*.o
rm -f obj_s/hardscroll.o obj_s/hashmap.o obj_s/tty_update.o obj_s/lib_mvcur.o

LOGFILE_PATH="$PWD/sym_ncurses.log" make libs CC="${PLATYPUS_CLANG}" \
  CFLAGS="-O3 -g -fpass-plugin=${STRUCT_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j8

rm -f lib/libncurses.so*
rm -f obj_s/lib_*.o
rm -f obj_s/hardscroll.o obj_s/hashmap.o obj_s/tty_update.o obj_s/lib_mvcur.o

cp ../dso_callbacks.cpp "${ROOT_DIR}/dso_callbacks.cpp"
cp ../BitMasks.cpp "${ROOT_DIR}/llvm-passes/BitMasks/BitMasks.cpp"
OLD_DIR="$PWD"
cd "${ROOT_DIR}/llvm-passes/BitMasks/build"
make -j8
cd "$OLD_DIR"

cat > ../libraries.json <<EOF
{
    "${ROOT_DIR}/libraries/instrumented_libs/libtinfo.so.6.3": "INFO",
    "${ROOT_DIR}/libraries/instrumented_libs/libc.so.6": "LIBC",
    "${ROOT_DIR}/libraries/instrumented_libs/ld-linux-x86-64.so.2": "LD"
}
EOF



PROTECT_JMP=True make libs CC=${PLATYPUS_CLANG} \
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


[ -f sym_ncurses.log ] || touch sym_ncurses.log
[ -f dynsym_ncurses.log ] || touch dynsym_ncurses.log

python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym_ncurses.log \
  lib/libncurses.so.6.3 \
  0 \
  CURS \
  reachable_structs \
  sym_ncurses.log \
  > output_ncurses.txt

cp lib/libncurses.so.6.3 ../../instrumented_libs
ln -sf libncurses.so.6.3 ../../instrumented_libs/libncurses.so.6 

python3 "$SCRIPT_2" output_ncurses.txt header_ncurses.txt