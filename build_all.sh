#!/usr/bin/env bash

# Enter llvm-platypus directory
cd llvm-platypus

# Run compile script
./compiler.sh "$PWD/../" "$PWD/../"
chmod +x ./compiler_uninstrumented.sh
./compiler_uninstrumented.sh "$PWD/../" "$PWD/../"

cd ..

# Build the 3 compiler passes

export LLVM_ROOT=$PWD/llvm-project
export ROOT_DIR=$PWD

cd llvm-passes/LogStructs

mkdir -p build
cd build

cmake ../
make -j"$(nproc)"

cd ../../FindDynSym

mkdir -p build
cd build

cmake ../
make -j"$(nproc)"

cd ../../BitMasks

mkdir -p build
cd build

cmake ../
make -j"$(nproc)"

cd ../../..


# Compile glibc

mkdir -p libraries/instrumented_libs
mkdir -p libraries/artifact_libs_uninstrumented

mkdir -p binaries/artifact_binaries_uninstrumented
mkdir -p binaries/artifact_binaries_instrumented

export PATH="$PWD/llvm-project-uninstrumented/build/bin:$PATH"

cd glibc-platypus
chmod +x libc_build.sh
./libc_build.sh
cd ..


cp glibc-platypus/build/libc.so libraries/instrumented_libs/libc.so.6
cp glibc-platypus/build/elf/ld.so libraries/instrumented_libs/ld-linux-x86-64.so.2

cp glibc-platypus/build-uninstrumented/libc.so libraries/artifact_libs_uninstrumented/libc.so.6
cp glibc-platypus/build-uninstrumented/elf/ld.so libraries/artifact_libs_uninstrumented/ld-linux-x86-64.so.2

bin="glibc-platypus/build/elf/ld.so"
symbol="_dl_fini"
out="header.txt"

addr=$(
  nm -a "$bin" | awk -v sym="$symbol" '
    $3 == sym && !found {
      print $1
      found=1
    }
    END {
      if (!found) exit 1
    }
  '
)

# strip leading zeros
addr=$(printf '%s' "$addr" | sed 's/^0*//')
[ -z "$addr" ] && addr="0"

printf "{'LIBC': ['LOD_0x%s']}\n" "$addr" > "$out"


# Compile libraries

export LLVM_ROOT=$PWD/llvm-project
export ROOT_DIR=$PWD

cd libraries/

for dir in pcre2 libcrypt libevent libreadline openssl libz libtinfo libncurses; do
    cd "$dir"
    chmod +x compile.sh
    ./compile.sh
    cd ..
done

ln -sf instrumented_libs/ld-linux-x86-64.so.2 ld.so

cd ..


cp /lib/x86_64-linux-gnu/libcap.so* ./libraries/instrumented_libs/
cp /lib/x86_64-linux-gnu/libnss_sss.so* ./libraries/instrumented_libs/
cp /lib/x86_64-linux-gnu/libnss_systemd.so* ./libraries/instrumented_libs/
cp /lib/x86_64-linux-gnu/libstdc++.so* ./libraries/instrumented_libs/
cp /lib/x86_64-linux-gnu/libnss_mdns4_minimal.so* ./libraries/instrumented_libs/
cp /lib/x86_64-linux-gnu/libgcc_s.so* ./libraries/instrumented_libs/
cp /lib/x86_64-linux-gnu/libm.so* ./libraries/instrumented_libs/

cp /lib/x86_64-linux-gnu/libcap.so* ./libraries/artifact_libs_uninstrumented/
cp /lib/x86_64-linux-gnu/libnss_sss.so* ./libraries/artifact_libs_uninstrumented/
cp /lib/x86_64-linux-gnu/libnss_systemd.so* ./libraries/artifact_libs_uninstrumented/
cp /lib/x86_64-linux-gnu/libstdc++.so* ./libraries/artifact_libs_uninstrumented/
cp /lib/x86_64-linux-gnu/libnss_mdns4_minimal.so* ./libraries/artifact_libs_uninstrumented/
cp /lib/x86_64-linux-gnu/libgcc_s.so* ./libraries/artifact_libs_uninstrumented/
cp /lib/x86_64-linux-gnu/libm.so* ./libraries/artifact_libs_uninstrumented/


# Compile binaries
cd binaries

for dir in bftpd redis nginx memcached lighttpd sqlite; do
    cd "$dir"
    chmod +x compile.sh
    ./compile.sh
    cd ..
done