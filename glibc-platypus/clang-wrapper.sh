#!/bin/sh

HAS_LIBC=0
HAS_LIBM=0
HAS_LOADER=0

# Check if -DMODULE_NAME=libc is present in the command line args
for arg in "$@"; do
    if [ "$arg" = "-DMODULE_NAME=libc" ]; then
        HAS_LIBC=1
        break
    fi
    if [ "$arg" = "-DMODULE_NAME=libm" ]; then
        HAS_LIBM=1
    fi
    if [ "$arg" = "-DMODULE_NAME=rtld" ]; then
        HAS_LOADER=1
    fi
done

MYPASS="-O2 -g -fcf-protection=full -fPIC -include masks.h -fpass-plugin=${ROOT_DIR}/llvm-passes/BitMasks/build/libBitmask.so -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,-z,relro,-z,now"
#MYPASS="-fPIC -fpass-plugin=${ROOT_DIR}/llvm-passes/FindDynSym/build/libDynsym.so -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,-z,relro,-z,now"
#MYPASS="-O2 -g -fPIC -fpass-plugin=${ROOT_DIR}/llvm-passes/LogStructs/build/libLogstructs.so -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,-z,relro,-z,now"

RTLD="-O2 -g -fcf-protection=full -fPIC -include masks_ld.h -fpass-plugin=${ROOT_DIR}/llvm-passes/BitMasks/build/libBitmask.so -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,-z,nodefs,-z,relro,-z,now"
REST="-O2 -fPIC -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,-z,relro,-z,now"

if [ "$HAS_LIBC" -eq 1 ]; then
    # Only for libc, add the plugin
    export PROTECT_JMP=True
    exec clang -Qunused-arguments $MYPASS "$@"
elif [ "$HAS_LOADER" -eq 1 ]; then
    export LOADER=1
    exec clang -Qunused-arguments $RTLD "$@"
else
    exec clang -Qunused-arguments $REST "$@"
fi