#!/usr/bin/env bash

export LLVM_ROOT=$PWD/llvm-project
export ROOT_DIR=$PWD


git restore glibc-platypus
git clean -fd glibc-platypus

git restore binaries
git clean -ffd binaries
git clean -ffd binaries

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

addr=$(printf '%s' "$addr" | sed 's/^0*//')
[ -z "$addr" ] && addr="0"

printf "{'LIBC': ['LOD_0x%s']}\n" "$addr" > "$out"


cd libraries/

ln -sf instrumented_libs/ld-linux-x86-64.so.2 ld.so

cd artifact_libs_uninstrumented
ln -sfn libcrypt.so.1.1.0       libcrypt.so.1
ln -sfn libevent-2.1.so.7.0.1   libevent-2.1.so.7
ln -sfn libncurses.so.6.3       libncurses.so.6
ln -sfn libpcre2-8.so.0.12.0    libpcre2-8.so.0
ln -sfn libreadline.so.8.3      libreadline.so.8
ln -sfn libtinfo.so.6.3         libtinfo.so.6
ln -sfn libz.so.1.3.1           libz.so.1

cd ../..


cd binaries

for dir in bftpd redis nginx memcached lighttpd sqlite; do
    cd "$dir"
    chmod +x compile.sh
    ./compile.sh
    cd ..
done