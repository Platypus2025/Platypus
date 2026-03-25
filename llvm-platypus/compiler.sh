#!/bin/bash

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 PATH_TO_PLATYPUS TARGET_DIR"
    exit 1
fi


PATH_TO_PLATYPUS="$1"
TARGET_DIR="$2"


cd "$TARGET_DIR" || { echo "Failed to cd into $CLONE_DIR"; exit 1; }

# Clone only if not already present
if [ ! -d "llvm-project" ]; then
    echo "Cloning llvm-project..."
    git clone https://github.com/llvm/llvm-project.git
else
    echo "llvm-project already exists, skipping clone."
fi


cd llvm-project/

git checkout llvmorg-20.1.0


LLD_FILES="${PATH_TO_PLATYPUS}/llvm-platypus/lld/ELF"
PATH_TO_LLVM="."

echo "PATH_TO_PLATYPUS: $PATH_TO_PLATYPUS"
echo "LLD_FILES: $LLD_FILES"
echo "PATH_TO_LLVM: $PATH_TO_LLVM"



cp "${LLD_FILES}/Driver.cpp" "${PATH_TO_LLVM}/lld/ELF/Driver.cpp"
cp "${LLD_FILES}/Config.h" "${PATH_TO_LLVM}/lld/ELF/Config.h"
cp "${LLD_FILES}/DriverUtils.cpp" "${PATH_TO_LLVM}/lld/ELF/DriverUtils.cpp"
cp "${LLD_FILES}/InputFiles.cpp" "${PATH_TO_LLVM}/lld/ELF/InputFiles.cpp"
cp "${LLD_FILES}/InputSection.cpp" "${PATH_TO_LLVM}/lld/ELF/InputSection.cpp"
cp "${LLD_FILES}/LinkerScript.cpp" "${PATH_TO_LLVM}/lld/ELF/LinkerScript.cpp"
cp "${LLD_FILES}/MarkLive.cpp" "${PATH_TO_LLVM}/lld/ELF/MarkLive.cpp"
cp "${LLD_FILES}/OutputSections.cpp" "${PATH_TO_LLVM}/lld/ELF/OutputSections.cpp"
cp "${LLD_FILES}/Relocations.cpp" "${PATH_TO_LLVM}/lld/ELF/Relocations.cpp"
cp "${LLD_FILES}/Symbols.cpp" "${PATH_TO_LLVM}/lld/ELF/Symbols.cpp"
cp "${LLD_FILES}/Symbols.h" "${PATH_TO_LLVM}/lld/ELF/Symbols.h"
cp "${LLD_FILES}/SymbolTable.cpp" "${PATH_TO_LLVM}/lld/ELF/SymbolTable.cpp"
cp "${LLD_FILES}/SymbolTable.h" "${PATH_TO_LLVM}/lld/ELF/SymbolTable.h"
cp "${LLD_FILES}/SyntheticSections.cpp" "${PATH_TO_LLVM}/lld/ELF/SyntheticSections.cpp"
cp "${LLD_FILES}/SyntheticSections.h" "${PATH_TO_LLVM}/lld/ELF/SyntheticSections.h"
cp "${LLD_FILES}/Target.h" "${PATH_TO_LLVM}/lld/ELF/Target.h"
cp "${LLD_FILES}/Writer.cpp" "${PATH_TO_LLVM}/lld/ELF/Writer.cpp"

cp "${LLD_FILES}/Arch/X86_64.cpp" "${PATH_TO_LLVM}/lld/ELF/Arch/X86_64.cpp"


cp "${LLD_FILES}/../../llvm/include/llvm/BinaryFormat/ELFRelocs/x86_64.def" "${PATH_TO_LLVM}/llvm/include/llvm/BinaryFormat/ELFRelocs/x86_64.def"

cp "${LLD_FILES}/../../llvm/lib/Target/X86/CMakeLists.txt" "${PATH_TO_LLVM}/llvm/lib/Target/X86/CMakeLists.txt"
cp "${LLD_FILES}/../../llvm/lib/Target/X86/X86CodeGenPassBuilder.cpp" "${PATH_TO_LLVM}/llvm/lib/Target/X86/X86CodeGenPassBuilder.cpp"
cp "${LLD_FILES}/../../llvm/lib/Target/X86/X86InstrumentJmpPass.cpp" "${PATH_TO_LLVM}/llvm/lib/Target/X86/X86InstrumentJmpPass.cpp"
cp "${LLD_FILES}/../../llvm/lib/Target/X86/X86InstrumentJmpPass.h" "${PATH_TO_LLVM}/llvm/lib/Target/X86/X86InstrumentJmpPass.h"
cp "${LLD_FILES}/../../llvm/lib/Target/X86/X86TargetMachine.cpp" "${PATH_TO_LLVM}/llvm/lib/Target/X86/X86TargetMachine.cpp"


mkdir -p build
cd build

cmake -G Ninja ../llvm \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_PROJECTS="clang;lld" \
    -DLLVM_TARGETS_TO_BUILD="X86" \
    -DCMAKE_INSTALL_PREFIX="$PWD/../install"

ninja -j"$(nproc)"