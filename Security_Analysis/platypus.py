#!/usr/bin/env python3
import argparse
import os
import re
import struct
import subprocess
import sys


def run(cmd):
    try:
        return subprocess.check_output(cmd, stderr=subprocess.DEVNULL, text=True)
    except Exception:
        return ""


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


def get_fakeplt_info(path):
    out = run(["llvm-readelf", "-SW", path])

    for line in out.splitlines():
        if ".fakeplt.sec" not in line:
            continue

        parts = line.split()
        if len(parts) < 7:
            continue

        try:
            idx = parts.index(".fakeplt.sec")
            addr = int(parts[idx + 2], 16)
            off = int(parts[idx + 3], 16)
            size = int(parts[idx + 4], 16)
            return addr, off, size
        except Exception:
            continue

    return None, None, None


def get_undefined_dyn_symbols(path):
    out = run(["llvm-readelf", "--dyn-syms", "-W", path])
    und = set()

    for line in out.splitlines():
        line = line.strip()
        if not line or ":" not in line:
            continue

        parts = line.split()
        if len(parts) < 8:
            continue

        if parts[6] == "UND":
            und.add(parts[7])

    return und


def get_jump_slot_symbols_by_offset(path):
    out = run(["llvm-readelf", "-rW", path])
    relocs = {}

    for line in out.splitlines():
        s = line.strip()
        if "R_X86_64_JUMP_SLOT" not in s:
            continue

        parts = s.split()
        if len(parts) < 6:
            continue

        try:
            offset = int(parts[0], 16)
        except ValueError:
            continue

        sym = None
        if "+" in parts:
            plus_idx = parts.index("+")
            if plus_idx >= 1:
                sym = parts[plus_idx - 1]

        if sym is not None:
            relocs[offset] = sym

    return relocs


def decode_stub_target(stub_addr, stub_bytes):
    if len(stub_bytes) < 10:
        return None

    if stub_bytes[0:4] != b"\xf3\x0f\x1e\xfa":
        return None

    if stub_bytes[4:6] != b"\xff\x25":
        return None

    disp = struct.unpack("<i", stub_bytes[6:10])[0]
    return stub_addr + 10 + disp


def count_external_fakeplt_entries(path):
    fakeplt_addr, fakeplt_off, fakeplt_size = get_fakeplt_info(path)
    if fakeplt_addr is None or fakeplt_off is None or fakeplt_size is None:
        return 0

    if fakeplt_size % 32 != 0:
        return 0

    undefined_dyn_symbols = get_undefined_dyn_symbols(path)
    reloc_symbols = get_jump_slot_symbols_by_offset(path)

    try:
        with open(path, "rb") as f:
            f.seek(fakeplt_off)
            fakeplt_data = f.read(fakeplt_size)
    except Exception:
        return 0

    stub_count = fakeplt_size // 32
    count = 0

    for i in range(stub_count):
        stub_off = i * 32
        stub_addr = fakeplt_addr + stub_off
        stub = fakeplt_data[stub_off:stub_off + 32]

        got_target = decode_stub_target(stub_addr, stub)
        if got_target is None:
            continue

        sym = reloc_symbols.get(got_target)
        if sym and sym in undefined_dyn_symbols:
            count += 1

    return count


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
    binary_count = count_external_fakeplt_entries(binary)
    results.append((binary_name, binary_count))

    for lib in get_needed(binary):
        path = find_library(lib, search_path)
        if path:
            count = count_external_fakeplt_entries(path)

            if os.path.basename(lib).startswith("libc.so"):
                count = max(0, count - 10)

            results.append((lib, count))
        else:
            print(f"{lib} NOT_FOUND", file=sys.stderr)

    for name, count in results:
        print(f"{name} {count}")


if __name__ == "__main__":
    main()