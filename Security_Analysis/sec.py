#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys


def run_cmd(cmd):
    try:
        return subprocess.check_output(cmd, text=True, stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        print(f"failed to run: {' '.join(cmd)}", file=sys.stderr)
        sys.exit(1)
    except FileNotFoundError:
        print(f"command not found: {cmd[0]}", file=sys.stderr)
        sys.exit(1)


def parse_ct_output(text):
    data = {}
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        tag = parts[0]
        try:
            value = int(parts[1])
        except ValueError:
            continue
        data[tag] = value
    return data


def parse_platypus_output(text):
    data = {}
    order = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        name = parts[0]
        try:
            value = int(parts[1])
        except ValueError:
            continue
        data[name] = value
        order.append(name)
    return data, order


def parse_cet_output(text):
    """
    cet.py / end.py format:
      name own_count total_minus_own

    We use own_count and recompute CET ourselves so filtering by selected
    libraries works correctly.
    """
    own_counts = {}
    order = []

    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue

        parts = line.split()
        if len(parts) < 2:
            continue

        name = parts[0]
        try:
            own_count = int(parts[1])
        except ValueError:
            continue

        own_counts[name] = own_count
        order.append(name)

    return own_counts, order


def lib_to_tag(name):
    base = os.path.basename(name)

    if base.startswith("libc.so"):
        return "LIBC"
    if base.startswith("libz.so"):
        return "LIBZ"
    if base.startswith("libssl.so"):
        return "LSSL"
    if base.startswith("libcrypto.so"):
        return "CRYPTO"
    if "pcre" in base:
        return "PCR"
    if base.startswith("libreadline.so"):
        return "READ"
    if base.startswith("libncurses.so"):
        return "CURS"
    if base.startswith("libtinfo.so"):
        return "INFO"

    return None

def normalize_name(name):
    if ".so" in name:
        return name.split(".so")[0] + ".so"
    return name


def format_pct(cet, platypus):
    if cet == 0:
        return "0.00"
    return f"{((cet - platypus) / cet) * 100:.2f}"


def is_selected(module_name, selected_names):
    """
    Accept either raw or normalized names.
    Examples:
      libc.so.6 matches libc.so.6 or libc.so
      libssl.so.3 matches libssl.so
    """
    if not selected_names:
        return True

    raw = module_name
    norm = normalize_name(module_name)
    return raw in selected_names or norm in selected_names


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("ct_input")
    parser.add_argument("platypus_search_path")
    parser.add_argument("cet_search_path")
    parser.add_argument(
        "libs",
        nargs="*",
        help="Optional list of libraries/modules to include. "
             "If omitted, all modules are used."
    )
    parser.add_argument("--ct-script", default="./ct.py")
    parser.add_argument("--platypus-script", default="./platypus.py")
    parser.add_argument("--cet-script", default="./cet.py")
    args = parser.parse_args()

    ct_out = run_cmd(["python3", args.ct_script, args.ct_input])
    platypus_out = run_cmd(
        ["python3", args.platypus_script, args.binary, args.platypus_search_path]
    )
    cet_out = run_cmd(
        ["python3", args.cet_script, args.binary, args.cet_search_path]
    )

    ct_data = parse_ct_output(ct_out)
    platypus_data, platypus_order = parse_platypus_output(platypus_out)
    cet_own_data, cet_order = parse_cet_output(cet_out)

    modules = cet_order[:]
    for name in platypus_order:
        if name not in modules:
            modules.append(name)

    selected_names = set(args.libs)
    selected_modules = [m for m in modules if is_selected(m, selected_names)]

    total_selected_own = sum(cet_own_data.get(m, 0) for m in selected_modules)

    rows = []
    for mod in selected_modules:
        own = cet_own_data.get(mod, 0)
        cet = total_selected_own - own

        tag = lib_to_tag(mod)
        ct = ct_data.get(tag, 0) if tag else 0
        plat = platypus_data.get(mod, 0)
        platypus = plat + ct
        red = format_pct(cet, platypus)

        rows.append((normalize_name(mod), cet, platypus, ct, red))

    print(f"{'Module':<20} {'CET':>8} {'PLATYPUS':>10} {'CT':>8} {'Red. (%)':>10}")
    print("-" * 62)
    for mod, cet, platypus, ct, red in rows:
        print(f"{mod:<20} {cet:>8} {platypus:>10} {ct:>8} {red:>10}")


if __name__ == "__main__":
    main()