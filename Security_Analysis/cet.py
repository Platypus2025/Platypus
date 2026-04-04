#!/usr/bin/env python3
import argparse
import os
import re
import subprocess
import sys


def run(cmd):
    try:
        return subprocess.check_output(cmd, stderr=subprocess.DEVNULL, text=True)
    except Exception:
        return ""


def run_bytes(cmd):
    try:
        return subprocess.check_output(cmd, stderr=subprocess.DEVNULL)
    except Exception:
        return b""


def is_elf(path):
    try:
        with open(path, "rb") as f:
            return f.read(4) == b"\x7fELF"
    except Exception:
        return False


def get_needed(binary):
    out = run(["readelf", "-d", binary])
    libs = []

    for line in out.splitlines():
        m = re.search(r"\(NEEDED\).*Shared library: \[(.+?)\]", line)
        if m:
            libs.append(m.group(1))

    if libs:
        return libs

    out = run(["ldd", binary])
    for line in out.splitlines():
        if "=>" in line:
            libs.append(line.split("=>", 1)[0].strip())

    return libs


def get_soname(path):
    out = run(["readelf", "-d", path])
    for line in out.splitlines():
        m = re.search(r"\(SONAME\).*Library soname: \[(.+?)\]", line)
        if m:
            return m.group(1)
    return None


def count_endbr64(path):
    data = run_bytes(["objcopy", "--dump-section", ".text=/dev/stdout", path])
    if not data:
        with open(path, "rb") as f:
            data = f.read()
    return data.count(b"\xf3\x0f\x1e\xfa")


def find_library(dep, search_path):
    dep_base = os.path.basename(dep)
    matches = []

    for root, _, files in os.walk(search_path):
        for f in files:
            full = os.path.join(root, f)

            if not is_elf(full):
                continue

            soname = get_soname(full)

            if f == dep_base:
                return full

            if soname == dep:
                return full

            if dep_base in f:
                matches.append(full)
            elif soname and dep in soname:
                matches.append(full)

    if matches:
        return sorted(matches, key=len)[0]

    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("search_path")
    args = parser.parse_args()

    binary = args.binary
    search_path = args.search_path

    if not os.path.isfile(binary):
        print("binary not found", file=sys.stderr)
        sys.exit(1)

    if not os.path.isdir(search_path):
        print("search path not found", file=sys.stderr)
        sys.exit(1)

    results = []

    binary_name = os.path.basename(binary)
    binary_count = count_endbr64(binary)
    results.append((binary_name, binary_count))

    for lib in get_needed(binary):
        path = find_library(lib, search_path)
        if path:
            results.append((lib, count_endbr64(path)))
        else:
            print(f"{lib} NOT_FOUND", file=sys.stderr)

    total = sum(count for _, count in results)

    for name, count in results:
        print(f"{name} {count} {total - count}")


if __name__ == "__main__":
    main()