#!/usr/bin/env bash

SQLITE_VERSION="3500400"
SRC_DIR="sqlite-src-${SQLITE_VERSION}"
ZIPFILE="${SRC_DIR}.zip"
URL="https://www.sqlite.org/2025/${ZIPFILE}"

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

BIN="sqlite3"

DSO_CALLBACKS_FILE="${ROOT_DIR}/dso_callbacks.cpp"


SCRIPT_1="${ROOT_DIR}/scripts/callback_parser.py"
SCRIPT_2="${ROOT_DIR}/scripts/parse_output.py"
SCRIPT_3="${ROOT_DIR}/scripts/create_header.py"

# Make sure that the necessary libs are installed in the system, readline etc
# sudo apt install tcl-dev
# apt install build-essential pkg-config libreadline-dev libncurses-dev zlib1g-dev


echo "Downloading SQLite source ${SQLITE_VERSION}..."
wget "${URL}"

echo "Extracting ${ZIPFILE}..."
unzip "${ZIPFILE}"


echo "Entering source directory..."
cd "${SRC_DIR}"

echo "Running configure..."
CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./configure

echo "Building sqlite3..."
bear -- make sqlite3 -j"$(nproc)"

cp sqlite3 "${BIN_FOLDER_UNINSTRUMENTED}/"

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

make clean
make distclean


export PATH="${LLVM_ROOT}/build/bin:$PATH"

cat > ../libraries.json <<EOF
{
    "${ROOT_DIR}/libraries/instrumented_libs/libncurses.so.6.3": "CURS",
    "${ROOT_DIR}/libraries/instrumented_libs/libtinfo.so.6.3": "INFO",
    "${ROOT_DIR}/libraries/instrumented_libs/libreadline.so.8.3": "READ",
    "${ROOT_DIR}/libraries/instrumented_libs/libz.so.1.3.1": "LIBZ",
    "${ROOT_DIR}/libraries/instrumented_libs/libc.so.6": "LIBC",
    "${ROOT_DIR}/libraries/instrumented_libs/ld-linux-x86-64.so.2": "LD"
}
EOF



CC="${PLATYPUS_CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${PLATYPUS_LLD} -Wl,-z,relro,-z,now" \
./configure


PROTECT_JMP=True make "${BIN}" \
  CC="${PLATYPUS_CLANG} \
      -g -O3 -fPIC \
      -fcf-protection=full \
      -fpass-plugin=${MASK_PLUGIN} \
      -fuse-ld=lld \
      -rdynamic \
      -Wl,-z,relro,-z,now \
      -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
      -Wl,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
      -Wl,--callb_getter=${PWD}/../a.txt \
      -Wl,--allow-shlib-undefined" \
  -j8

touch reachable_structs

python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  sqlite3 \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt

python3 "$SCRIPT_2" output.txt header_sqlite3.txt bin
sed -i '$ s/}/, '\''LIBZ'\'':[], '\''INFO'\'':[], '\''CURS'\'':[]}/' header_sqlite3.txt
cat "${ROOT_DIR}/libraries/libz/zlib-1.3.1/header.txt" >> header_sqlite3.txt
cat "${ROOT_DIR}/libraries/libncurses/ncurses-6.3/header_ncurses.txt" >> header_sqlite3.txt
cat "${ROOT_DIR}/libraries/libtinfo/tinfo/header_tinfo.txt" >> header_sqlite3.txt
cat "${ROOT_DIR}/libraries/libreadline/readline-8.3/header.txt" >> header_sqlite3.txt
cat "${ROOT_DIR}/header.txt" >> header_sqlite3.txt
python3 "$SCRIPT_3" header_sqlite3.txt fallthrough libm.so.6

clang -c mask.c -fcf-protection=full

rm sqlite3

PROTECT_JMP=True make "${BIN}" \
  CC="${PLATYPUS_CLANG} \
      -g -O3 -fPIC \
      -fcf-protection=full \
      -fpass-plugin=${MASK_PLUGIN} \
      -fuse-ld=lld \
      -rdynamic \
      -Wl,-z,relro,-z,now \
      -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
      -Wl,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
      -Wl,--callb_getter=${PWD}/../a.txt \
      -Wl,--allow-shlib-undefined \
      ${PWD}/mask.o" \
  -j8



python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  sqlite3 \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt

python3 "$SCRIPT_2" output.txt header_sqlite3.txt bin
sed -i '$ s/}/, '\''LIBZ'\'':[], '\''INFO'\'':[], '\''CURS'\'':[]}/' header_sqlite3.txt
cat "${ROOT_DIR}/libraries/libz/zlib-1.3.1/header.txt" >> header_sqlite3.txt
cat "${ROOT_DIR}/libraries/libncurses/ncurses-6.3/header_ncurses.txt" >> header_sqlite3.txt
cat "${ROOT_DIR}/libraries/libtinfo/tinfo/header_tinfo.txt" >> header_sqlite3.txt
cat "${ROOT_DIR}/libraries/libreadline/readline-8.3/header.txt" >> header_sqlite3.txt
cat "${ROOT_DIR}/header.txt" >> header_sqlite3.txt
python3 "$SCRIPT_3" header_sqlite3.txt fallthrough libm.so.6

clang -c mask.c -fcf-protection=full

rm sqlite3

PROTECT_JMP=True make "${BIN}" \
  CC="${PLATYPUS_CLANG} \
      -g -O3 -fPIC \
      -fcf-protection=full \
      -fpass-plugin=${MASK_PLUGIN} \
      -fuse-ld=lld \
      -rdynamic \
      -Wl,-z,relro,-z,now \
      -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
      -Wl,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
      -Wl,--callb_getter=${PWD}/../a.txt \
      -Wl,--allow-shlib-undefined \
      ${PWD}/mask.o" \
  -j8

cp sqlite3 ../../artifact_binaries_instrumented

# make clean
# make distclean
# CC="${CLANG}" \
# CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
# LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
# ./configure

# make sqlite3 -j"$(nproc)"

# make TCL_CONFIG_SH=/usr/lib/x86_64-linux-gnu/tcl8.6/tclConfig.sh test -j8