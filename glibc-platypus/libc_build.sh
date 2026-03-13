#!/usr/bin/env bash
set -euo pipefail

export PATH="${PWD}/../llvm-project/build/bin:$PATH"
export ROOT_DIR="${PWD}/../"

cp dso_callbacks.cpp "${PWD}/../dso_callbacks.cpp"
cp BitMasks.cpp "${ROOT_DIR}/llvm-passes/BitMasks/BitMasks.cpp"
OLD_DIR="$PWD"
cd "${ROOT_DIR}/llvm-passes/BitMasks/build"
make -j8
cd "$OLD_DIR"

FILE="stdlib/qsort.c"

tmp=$(mktemp)

head -n -2 "$FILE" > "$tmp"

tail -n 2 "$FILE" | while IFS= read -r line; do
    echo "$line" | sed 's|^[[:space:]]*//||' >> "$tmp"
done

mv "$tmp" "$FILE"


mkdir -p build
cd build

CC=clang ../configure --prefix=/a/path --disable-static --disable-docs --enable-cet

cp ../clang-wrapper.sh .
chmod +x clang-wrapper.sh

cp ../masks.h ./
cp ../masks_ld.h ./

make CC="$PWD/clang-wrapper.sh" -j8

awk '/^[[:space:]]*global:[[:space:]]*$/ { print; print "    LIBC;"; next } { print }' \
    libc.map > libc.map.new
mv libc.map.new libc.map

sed -i '/GLIBC_PRIVATE {/,/local:/ s/global:/global:\n    LOD;/' ld.map

make CC="$PWD/clang-wrapper.sh" -j8

export PATH="${PATH#*:}"

cd ..

FILE="stdlib/qsort.c"

tmp=$(mktemp)

head -n -2 "$FILE" > "$tmp"

tail -n 2 "$FILE" | while IFS= read -r line; do
    if [[ "$line" =~ ^[[:space:]]*// ]]; then
        echo "$line" >> "$tmp"
    else
        echo "//${line}" >> "$tmp"
    fi
done

mv "$tmp" "$FILE"


mkdir -p build-uninstrumented
cd build-uninstrumented

cp ../clang-wrapper-uninstrumented.sh ./
chmod +x clang-wrapper-uninstrumented.sh

CC=clang ../configure --prefix=/a/path --disable-static --disable-docs --enable-cet

make CC="$PWD/clang-wrapper-uninstrumented.sh" -j8

cd ..