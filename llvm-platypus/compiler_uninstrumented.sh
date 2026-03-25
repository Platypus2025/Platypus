#!/bin/bash

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 PATH_TO_PLATYPUS TARGET_DIR"
    exit 1
fi

PATH_TO_PLATYPUS="$1"
TARGET_DIR="$2"

cd "$TARGET_DIR" || { echo "Failed to cd into $TARGET_DIR"; exit 1; }

# Clone into llvm-project-uninstrumented
if [ ! -d "llvm-project-uninstrumented" ]; then
    echo "Cloning llvm-project (uninstrumented)..."
    git clone https://github.com/llvm/llvm-project.git llvm-project-uninstrumented
else
    echo "llvm-project-uninstrumented already exists, skipping clone."
fi

cd llvm-project-uninstrumented || exit 1

git checkout llvmorg-20.1.0

mkdir -p build
cd build || exit 1

cmake -G Ninja ../llvm \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_PROJECTS="clang;lld" \
    -DLLVM_TARGETS_TO_BUILD="X86" \
    -DCMAKE_INSTALL_PREFIX="$PWD/../install"

ninja -j "$(nproc)"