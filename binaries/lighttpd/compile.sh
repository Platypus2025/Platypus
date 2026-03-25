#!/usr/bin/env bash

LIGHTTPD_VERSION="1.4.79"
TARBALL="lighttpd-${LIGHTTPD_VERSION}.tar.gz"
URL="https://download.lighttpd.net/lighttpd/releases-1.4.x/${TARBALL}"

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

BIN="lighttpd"

DSO_CALLBACKS_FILE="${ROOT_DIR}/dso_callbacks.cpp"

SCRIPT_1="${ROOT_DIR}/scripts/callback_parser.py"
SCRIPT_2="${ROOT_DIR}/scripts/parse_output.py"
SCRIPT_3="${ROOT_DIR}/scripts/create_header.py"


echo "Downloading lighttpd ${LIGHTTPD_VERSION}..."
wget -O "${TARBALL}" "${URL}"

echo "Extracting ${TARBALL}..."
rm -rf "lighttpd-${LIGHTTPD_VERSION}"
tar -xvf "${TARBALL}"

echo "Entering source directory..."
cd "lighttpd-${LIGHTTPD_VERSION}"

echo "Installing static module list..."
cat > src/plugin-static.h <<'EOF'
/* This file is included from plugin.c, after PLUGIN_INIT is defined. */
/* Just put one module per line, NO semicolons! */

PLUGIN_INIT(mod_rewrite)
PLUGIN_INIT(mod_redirect)
PLUGIN_INIT(mod_access)
PLUGIN_INIT(mod_alias)
PLUGIN_INIT(mod_indexfile)
PLUGIN_INIT(mod_staticfile)
PLUGIN_INIT(mod_setenv)
PLUGIN_INIT(mod_expire)
PLUGIN_INIT(mod_simple_vhost)
PLUGIN_INIT(mod_evhost)
PLUGIN_INIT(mod_fastcgi)
PLUGIN_INIT(mod_scgi)
PLUGIN_INIT(mod_accesslog)
PLUGIN_INIT(mod_auth)
PLUGIN_INIT(mod_extforward)
PLUGIN_INIT(mod_authn_file)
PLUGIN_INIT(mod_cgi)
PLUGIN_INIT(mod_status)
PLUGIN_INIT(mod_dirlisting)
PLUGIN_INIT(mod_proxy)
PLUGIN_INIT(mod_deflate)
EOF

echo "Generating configure script..."
bash ./autogen.sh

echo "Running configure for static lighttpd modules, dynamic system libs..."
LIGHTTPD_STATIC=yes \
CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./configure -C --enable-static=yes

echo "Building lighttpd with statically linked modules..."
bear -- make build_static=1 -j"$(nproc)"

echo "Copying built binary..."
cp src/lighttpd "${BIN_FOLDER_UNINSTRUMENTED}/"


make clean

LIGHTTPD_STATIC=yes \
CC="${PLATYPUS_CLANG}" \
CFLAGS="-g -fPIC -O3 -fpass-plugin=${DYNSYM_PLUGIN}" \
LDFLAGS="-fuse-ld=${PLATYPUS_LLD} -Wl,--allow-shlib-undefined" \
./configure -C --enable-static=yes

cat > src/versionstamp.h <<'EOF'
#ifndef VERSIONSTAMP_H
#define VERSIONSTAMP_H
#define REPO_VERSION ""
#endif
EOF

LOGFILE_PATH="$PWD/dynsym.log" \
make -j1 -C src build_static=1 "${BIN}" \
  CC="${PLATYPUS_CLANG}" \
  CFLAGS="-g -fPIC -O3 -fpass-plugin=${DYNSYM_PLUGIN}" \
  LDFLAGS="-fuse-ld=${PLATYPUS_LLD} -Wl,--allow-shlib-undefined"

"${ANNOTATED_SCRIPT}" annon.log ./

make clean

cat > src/versionstamp.h <<'EOF'
#ifndef VERSIONSTAMP_H
#define VERSIONSTAMP_H
#define REPO_VERSION ""
#endif
EOF

LOGFILE_PATH="$PWD/sym.log" make -j1 -C src build_static=1 "${BIN}" \
  CC="${PLATYPUS_CLANG}" \
  CFLAGS="-O3 -g -fpass-plugin=${STRUCT_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
  -j8


make clean

cat > src/versionstamp.h <<'EOF'
#ifndef VERSIONSTAMP_H
#define VERSIONSTAMP_H
#define REPO_VERSION ""
#endif
EOF

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
    "${ROOT_DIR}/libraries/instrumented_libs/libz.so.1.3.1": "LIBZ",
    "${ROOT_DIR}/libraries/instrumented_libs/libpcre2-8.so.0.12.0": "PCR",
    "${ROOT_DIR}/libraries/instrumented_libs/libc.so.6": "LIBC",
    "${ROOT_DIR}/libraries/instrumented_libs/ld-linux-x86-64.so.2": "LD"
}
EOF




PROTECT_JMP=True make -j1 -C src build_static=1 "${BIN}" \
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
  src/lighttpd \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt

python3 "$SCRIPT_2" output.txt header.txt bin
grep -q "'LIBZ'" yourfile || sed -i "\$s/}/, 'LIBZ':[], 'PCR':[]}/" header.txt
cat "${ROOT_DIR}/libraries/libz/zlib-1.3.1/header.txt" >> header.txt
cat "${ROOT_DIR}/libraries/libcrypt/libxcrypt-4.4.38/header.txt" >> header.txt
cat "${ROOT_DIR}/libraries/pcre2/pcre2-10.43/header.txt" >> header.txt
cat "${ROOT_DIR}/header.txt" >> header.txt

python3 "$SCRIPT_3" header.txt

clang -c mask.c -fcf-protection=full

rm src/lighttpd

PROTECT_JMP=True make -j1 -C src build_static=1 "${BIN}" \
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
           -Wl,--allow-shlib-undefined $PWD/mask.o" \
  -j8

python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  src/lighttpd \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt

python3 "$SCRIPT_2" output.txt header.txt bin
grep -q "'LIBZ'" yourfile || sed -i "\$s/}/, 'LIBZ':[], 'PCR':[]}/" header.txt
cat "${ROOT_DIR}/libraries/libz/zlib-1.3.1/header.txt" >> header.txt
cat "${ROOT_DIR}/libraries/libcrypt/libxcrypt-4.4.38/header.txt" >> header.txt
cat "${ROOT_DIR}/libraries/pcre2/pcre2-10.43/header.txt" >> header.txt
cat "${ROOT_DIR}/header.txt" >> header.txt

python3 "$SCRIPT_3" header.txt

clang -c mask.c -fcf-protection=full

rm src/lighttpd

PROTECT_JMP=True make -j1 -C src build_static=1 "${BIN}" \
  CC=${PLATYPUS_CLANG} \
  CFLAGS="-fPIC -g \
          -O3 \
          -fcf-protection=full \
          -fpass-plugin=${MASK_PLUGIN}" \
  LDFLAGS="-fuse-ld=lld \
           -rdynamic \
           -Wl,-z,relro,-z,now \
           -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
           -Wl,--strip-debug \
           -Wl,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
           -Wl,--allow-shlib-undefined $PWD/mask.o" \
  -j8

cp src/lighttpd ../../artifact_binaries_instrumented


# make clean
# echo "Running configure for static lighttpd modules, dynamic system libs..."
# LIGHTTPD_STATIC=yes \
# CC="${CLANG}" \
# CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
# LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
# ./configure -C --enable-static=yes

# echo "Building lighttpd with statically linked modules..."
# bear -- make build_static=1 -j"$(nproc)"