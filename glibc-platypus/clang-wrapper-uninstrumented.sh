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


RTLD="-O2 -g -fcf-protection=full -fPIC -fuse-ld=/usr/bin/ld.lld-20 -Wl,--allow-shlib-undefined -Wl,-z,nodefs,-z,relro,-z,now"

if [ "$HAS_LIBC" -eq 1 ]; then
    exec clang -Qunused-arguments $RTLD "$@"
elif [ "$HAS_LOADER" -eq 1 ]; then
    export LOADER=1
    exec clang -Qunused-arguments $RTLD "$@"
else
    exec clang -Qunused-arguments $RTLD "$@"
fi