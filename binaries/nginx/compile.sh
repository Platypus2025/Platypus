NGINX_VERSION="1.28.0"
SRC_DIR="nginx-${NGINX_VERSION}"
TARBALL="${SRC_DIR}.tar.gz"
URL="https://nginx.org/download/${TARBALL}"

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

BIN="nginx"

DSO_CALLBACKS_FILE="${ROOT_DIR}/dso_callbacks.cpp"

SCRIPT_1="${ROOT_DIR}/scripts/callback_parser.py"
SCRIPT_2="${ROOT_DIR}/scripts/parse_output.py"
SCRIPT_3="${ROOT_DIR}/scripts/create_header.py"

echo "Downloading nginx source ${NGINX_VERSION}..."
wget "${URL}"

echo "Extracting ${TARBALL}..."
tar -xzf "${TARBALL}"

echo "Entering source directory..."
cd "${SRC_DIR}"

echo "Running configure..."
CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./configure \
    --prefix=/usr/local/nginx \
    --with-http_ssl_module \
    --with-http_gzip_static_module \
    --with-http_stub_status_module \
    --with-http_v2_module \
    --with-stream \
    --with-http_realip_module \
    --with-stream_ssl_module

echo "Building nginx..."
bear -- make -j"$(nproc)"

cp objs/nginx "${BIN_FOLDER_UNINSTRUMENTED}/"

make clean
CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./configure \
    --prefix=/usr/local/nginx \
    --with-http_ssl_module \
    --with-http_gzip_static_module \
    --with-http_stub_status_module \
    --with-http_v2_module \
    --with-stream \
    --with-http_realip_module \
    --with-stream_ssl_module

cp ../makefile objs/Makefile

export CC="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}"
export CFLAGS="-O3"
export LINK="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}"
export LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined"

LOGFILE_PATH="$PWD/dynsym.log" \
CC="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" \
CFLAGS="-O3" \
LINK="${PLATYPUS_CLANG} -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" \
LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" \
make -j1

"${ANNOTATED_SCRIPT}" annon.log ./

make clean
CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./configure \
    --prefix=/usr/local/nginx \
    --with-http_ssl_module \
    --with-http_gzip_static_module \
    --with-http_stub_status_module \
    --with-http_v2_module \
    --with-stream \
    --with-http_realip_module \
    --with-stream_ssl_module

cp ../makefile objs/Makefile

export LOGFILE_PATH="$PWD/sym.log"
export CC="${PLATYPUS_CLANG}"
export CFLAGS="-O3 -g -fpass-plugin=${STRUCT_PLUGIN}"
export LINK="${PLATYPUS_CLANG}"
export LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined"

make -j8

cp ../../dso_callbacks.cpp "${ROOT_DIR}/dso_callbacks.cpp"
cp ../../BitMasks.cpp "${ROOT_DIR}/llvm-passes/BitMasks/BitMasks.cpp"
OLD_DIR="$PWD"
cd "${ROOT_DIR}/llvm-passes/BitMasks/build"
make -j8
cd "$OLD_DIR"


make clean
CC="${CLANG}" \
CFLAGS="-fPIC -O3 -g -fcf-protection=full" \
LDFLAGS="-fuse-ld=${LD_LLD} -Wl,-z,relro,-z,now" \
./configure \
    --prefix=/usr/local/nginx \
    --with-http_ssl_module \
    --with-http_gzip_static_module \
    --with-http_stub_status_module \
    --with-http_v2_module \
    --with-stream \
    --with-http_realip_module \
    --with-stream_ssl_module

cp ../makefile objs/Makefile


export PATH="${LLVM_ROOT}/build/bin:$PATH"

cat > ../libraries.json <<EOF
{
    "${ROOT_DIR}/libraries/instrumented_libs/libcrypt.so.1.1.0": "CRYPT",
    "${ROOT_DIR}/libraries/instrumented_libs/libz.so.1.3.1": "LIBZ",
    "${ROOT_DIR}/libraries/instrumented_libs/libpcre2-8.so.0.12.0": "PCR",
    "${ROOT_DIR}/libraries/instrumented_libs/libssl.so.3": "LSSL",
    "${ROOT_DIR}/libraries/instrumented_libs/libcrypto.so.3": "CRYPTO",
    "${ROOT_DIR}/libraries/instrumented_libs/libc.so.6": "LIBC",
    "${ROOT_DIR}/libraries/instrumented_libs/ld-linux-x86-64.so.2": "LD"
}
EOF



export CC="${PLATYPUS_CLANG}"

export CFLAGS="-g -O3 -fPIC \
-fcf-protection=full \
-fpass-plugin=${MASK_PLUGIN}"

export LINK="${PLATYPUS_CLANG}"

export LDFLAGS="-fuse-ld=lld \
-rdynamic \
-Wl,-z,relro,-z,now \
-Wl,--dynamic-linker=${DYNAMIC_LINKER} \
-Wl,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
-Wl,--callb_getter=${PWD}/../a.txt \
-Wl,--allow-shlib-undefined"

PROTECT_JMP=True make -j8

# Nginx "imports" 2 struct datatypes that LIBZ exports -> callbacks could be passed through them
echo "LIBZ:internal_state,z_stream_s" > reachable_structs

python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  objs/nginx \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt

python3 "$SCRIPT_2" output.txt header.txt bin
# Important, libssl pass received callbacks to libcrypto so libcrypto should be ready to call them, transfer the callback table
# Same between libcrypto and libc
sed -i "/{'CRYPTO':'LSSL', 'LIBC':'CRYPTO'}/d" header.txt
sed -i "1s/.*/{'CRYPTO':'LSSL', 'LIBC':'CRYPTO'}/" header.txt
cat "${ROOT_DIR}/libraries/libz/zlib-1.3.1/header.txt" >> header.txt
cat "${ROOT_DIR}/libraries/libcrypt/libxcrypt-4.4.38/header.txt" >> header.txt
cat "${ROOT_DIR}/libraries/pcre2/pcre2-10.43/header.txt" >> header.txt
cat "${ROOT_DIR}/libraries/openssl/openssl-3.2.1/header_crypto.txt" >> header.txt
cat "${ROOT_DIR}/libraries/openssl/openssl-3.2.1/header_ssl.txt" >> header.txt
cat "${ROOT_DIR}/header.txt" >> header.txt

python3 "$SCRIPT_3" header.txt

clang -c mask.c -fcf-protection=full

rm objs/nginx

export LDFLAGS="-fuse-ld=lld \
-rdynamic \
-Wl,-z,relro,-z,now \
-Wl,--dynamic-linker=${DYNAMIC_LINKER} \
-Wl,-rpath,${LIBRARY_PATH_WITH_INSTRUMENTED} \
-Wl,--callb_getter=${PWD}/../a.txt \
-Wl,--allow-shlib-undefined ${PWD}/mask.o"

PROTECT_JMP=True make -j8

python3 "$SCRIPT_1" \
  ${PWD}/../libraries.json \
  dynsym.log \
  objs/nginx \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt

python3 "$SCRIPT_2" output.txt header.txt bin
# Important, libssl pass received callbacks to libcrypto so libcrypto should be ready to call them, transfer the callback table
# Same between libcrypto and libc
sed -i "/{'CRYPTO':'LSSL', 'LIBC':'CRYPTO'}/d" header.txt
sed -i "1s/.*/{'CRYPTO':'LSSL', 'LIBC':'CRYPTO'}/" header.txt
cat "${ROOT_DIR}/libraries/libz/zlib-1.3.1/header.txt" >> header.txt
cat "${ROOT_DIR}/libraries/libcrypt/libxcrypt-4.4.38/header.txt" >> header.txt
cat "${ROOT_DIR}/libraries/pcre2/pcre2-10.43/header.txt" >> header.txt
cat "${ROOT_DIR}/libraries/openssl/openssl-3.2.1/header_crypto.txt" >> header.txt
cat "${ROOT_DIR}/libraries/openssl/openssl-3.2.1/header_ssl.txt" >> header.txt
cat "${ROOT_DIR}/header.txt" >> header.txt
python3 "$SCRIPT_3" header.txt

clang -c mask.c -fcf-protection=full

rm objs/nginx

PROTECT_JMP=True make -j8

cp objs/nginx ../../artifact_binaries_instrumented