#!/usr/bin/env bash

REDIS_VERSION="8.2.0"

REPO_URL="https://github.com/redis/redis.git"

CLANG="/usr/bin/clang-20"
CLANGXX="/usr/bin/clang++-20"
LD_LLD="/usr/bin/ld.lld-20"
LIBRARY_PATH_WITH_INSTRUMENTED="${ROOT_DIR}/libraries/instrumented_libs"
DYNAMIC_LINKER="${LIBRARY_PATH_WITH_INSTRUMENTED}/ld-linux-x86-64.so.2"

PLATYPUS_CLANG="${ROOT_DIR}/llvm-project/build/bin/clang-20"
PLATYPUS_CLANGXX="${ROOT_DIR}/llvm-project/build/bin/clang++"
PLATYPUS_LLD="${ROOT_DIR}/llvm-project/build/bin/ld.lld"
DYNSYM_PLUGIN="${ROOT_DIR}/llvm-passes/FindDynSym/build/libDynsym.so"
STRUCT_PLUGIN="${ROOT_DIR}/llvm-passes/LogStructs/build/libLogstructs.so"
MASK_PLUGIN="${ROOT_DIR}/llvm-passes/BitMasks/build/libBitmask.so"

ANNOTATED_SCRIPT="${ROOT_DIR}/scripts/annotate.sh"
BIN_FOLDER_UNINSTRUMENTED="${ROOT_DIR}/binaries/artifact_binaries_uninstrumented"

BIN="redis-server"

DSO_CALLBACKS_FILE="${ROOT_DIR}/dso_callbacks.cpp"


SCRIPT_1="${ROOT_DIR}/scripts/callback_parser.py"
SCRIPT_2="${ROOT_DIR}/scripts/parse_output.py"
SCRIPT_3="${ROOT_DIR}/scripts/create_header.py"

echo "Cloning Redis repository at tag ${REDIS_VERSION}..."
git clone --branch "${REDIS_VERSION}" --depth 1 "${REPO_URL}"

echo "Entering repository..."
cd redis

echo "Verifying checked out tag..."
git describe --tags --exact-match


make MALLOC=libc USE_SYSTEMD=no -j"$(nproc)"
make clean

echo "Building Redis..."
bear -- make -j"$(nproc)" \
  CC="${CLANG}" \
  CXX="${CLANGXX}" \
  CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
  CXXFLAGS="-fPIC -O3 -g -fcf-protection=full" \
  LDFLAGS="-fuse-ld=lld -Wl,-z,relro,-z,now" \
  MALLOC=libc \
  USE_SYSTEMD=no

echo "Copying redis-server binary..."
cp src/redis-server "${BIN_FOLDER_UNINSTRUMENTED}/"

make clean


LOGFILE_PATH="$PWD/dynsym.log" \
make USE_SYSTEMD=no MALLOC=libc "${BIN}" \
  CC="${PLATYPUS_CLANG}" \
  CXX="${PLATYPUS_CLANGXX}" \
  CFLAGS="-O3 -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" \
  CXXFLAGS="-O3 -g -fPIC" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j1

"${ANNOTATED_SCRIPT}" annon.log ./

make clean


LOGFILE_PATH="$PWD/sym.log" \
make USE_SYSTEMD=no MALLOC=libc "${BIN}" \
  CC="${PLATYPUS_CLANG}" \
  CXX="${PLATYPUS_CLANGXX}" \
  CFLAGS="-O3 -g -fpass-plugin=${STRUCT_PLUGIN}" \
  CXXFLAGS="-O3 -g" \
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
    "${ROOT_DIR}/libraries/instrumented_libs/libc.so.6": "LIBC",
    "${ROOT_DIR}/libraries/instrumented_libs/ld-linux-x86-64.so.2": "LD"
}
EOF


PROTECT_JMP=True make USE_SYSTEMD=no MALLOC=libc "${BIN}" \
  CC="${PLATYPUS_CLANG}" \
  CXX="${PLATYPUS_CLANGXX}" \
  OPT="-O3 -fno-omit-frame-pointer" \
  CFLAGS="-fPIC -g \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  CXXFLAGS="-fPIC -g \
            -O3 \
            -fcf-protection=full" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,--disable-new-dtags,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
           -Wl,--allow-shlib-undefined \
           -Wl,--strip-debug \
           -Wl,--callb_getter=${PWD}/../a.txt" \
  -j8

touch reachable_structs

python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  src/redis-server \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt

python3 "$SCRIPT_2" output.txt header.txt bin

cat "${ROOT_DIR}/header.txt" >> header.txt

python3 "$SCRIPT_3" header.txt placeholder1__nss_mdns4_minimal_gethostbyname4_r fallthrough libgcc_s.so.1 libnss_mdns4_minimal.so.2 libstdc++.so.6 libm.so.6 \
  libc_fallthrough \
    libstdc++.so.6 \
    libgcc_s.so.1

clang -c mask.c -fcf-protection=full

rm src/redis-server


PROTECT_JMP=True make USE_SYSTEMD=no MALLOC=libc "${BIN}" \
  CC="${PLATYPUS_CLANG}" \
  CXX="${PLATYPUS_CLANGXX}" \
  OPT="-O3 -fno-omit-frame-pointer" \
  CFLAGS="-fPIC -g \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  CXXFLAGS="-fPIC -g \
            -O3 \
            -fcf-protection=full" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,--disable-new-dtags,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
           -Wl,--allow-shlib-undefined \
           -Wl,--strip-debug \
           -Wl,--callb_getter=${PWD}/../a.txt \
           ${PWD}/mask.o \
           -Wl,--push-state,--no-as-needed \
           ${LIBRARY_PATH_WITH_INSTRUMENTED}/libgcc_s.so.1 \
           ${LIBRARY_PATH_WITH_INSTRUMENTED}/libnss_mdns4_minimal.so.2 \
           -Wl,--pop-state" \
  -j8

export PATH="/home/pitogyro/Music/llvm-project/build/bin:$PATH"

python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  src/redis-server \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt

python3 "$SCRIPT_2" output.txt header.txt bin

cat "${ROOT_DIR}/header.txt" >> header.txt

python3 "$SCRIPT_3" header.txt placeholder1__nss_mdns4_minimal_gethostbyname4_r fallthrough libgcc_s.so.1 libnss_mdns4_minimal.so.2 libstdc++.so.6 libm.so.6 \
  libc_fallthrough \
    libstdc++.so.6 \
    libgcc_s.so.1

clang -c mask.c -fcf-protection=full

rm src/redis-server

PROTECT_JMP=True make USE_SYSTEMD=no MALLOC=libc "${BIN}" \
  CC="${PLATYPUS_CLANG}" \
  CXX="${PLATYPUS_CLANGXX}" \
  OPT="-O3" \
  CFLAGS="-fPIC -g \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,--disable-new-dtags,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
           -Wl,--allow-shlib-undefined \
           -Wl,--strip-debug \
           -Wl,--callb_getter=${PWD}/../a.txt \
           ${PWD}/mask.o \
           -Wl,--push-state,--no-as-needed \
           ${LIBRARY_PATH_WITH_INSTRUMENTED}/libgcc_s.so.1 \
           ${LIBRARY_PATH_WITH_INSTRUMENTED}/libnss_mdns4_minimal.so.2 \
           -Wl,--pop-state" \
  -j8

export PATH="${LLVM_ROOT}/build/bin:$PATH"

python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  src/redis-server \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt

python3 "$SCRIPT_2" output.txt header.txt bin

cat "${ROOT_DIR}/header.txt" >> header.txt

python3 "$SCRIPT_3" header.txt placeholder1__nss_mdns4_minimal_gethostbyname4_r fallthrough libgcc_s.so.1 libnss_mdns4_minimal.so.2 libstdc++.so.6 libm.so.6 \
  libc_fallthrough \
    libstdc++.so.6 \
    libgcc_s.so.1

clang -c mask.c -fcf-protection=full

rm src/redis-server

PROTECT_JMP=True make USE_SYSTEMD=no MALLOC=libc "${BIN}" \
  CC="${PLATYPUS_CLANG}" \
  CXX="${PLATYPUS_CLANGXX}" \
  OPT="-O3" \
  CFLAGS="-fPIC -g \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,--disable-new-dtags,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
           -Wl,--allow-shlib-undefined \
           -Wl,--strip-debug \
           -Wl,--callb_getter=${PWD}/../a.txt \
           ${PWD}/mask.o \
           -Wl,--push-state,--no-as-needed \
           ${LIBRARY_PATH_WITH_INSTRUMENTED}/libgcc_s.so.1 \
           ${LIBRARY_PATH_WITH_INSTRUMENTED}/libnss_mdns4_minimal.so.2 \
           -Wl,--pop-state" \
  -j8

cp src/redis-server ../../artifact_binaries_instrumented

# LC_ALL=C LANG=C make test -j8