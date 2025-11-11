#!/usr/bin/env python3

import subprocess
import re
import sys

def parse_rela_plt(binary_path):
    cmd = ["llvm-readelf", "-r", binary_path]
    output = subprocess.check_output(cmd, text=True)
    lines = output.splitlines()

    in_rela_plt = False
    parsed_lines = []
    for line in lines:
        if line.strip().startswith("Relocation section '.rela.plt'"):
            in_rela_plt = True
            continue
        if in_rela_plt:
            if line.strip().startswith("Relocation section") or not line.strip():
                break
            if re.match(r"^[0-9a-fA-F]{16}", line):
                parsed_lines.append(line.strip())

    offset2symbol = {}
    symbol2offset = {}

    for line in parsed_lines:
        cols = line.split()
        if len(cols) < 6:
            continue
        symbol_value = cols[3]
        if symbol_value != '0000000000000000':
            continue
        m = re.match(r'^([0-9a-fA-F]{16})\s+.*?([A-Za-z0-9_.]+)(@[^ \t]+)?(\s+\+ 0)?\s*$', line)
        if m:
            offset = m.group(1).lower()
            symbol = m.group(2)
            offset2symbol[offset] = symbol
            symbol2offset[symbol] = offset
    return offset2symbol, symbol2offset

def parse_plt_section(binary_path, section, offset2symbol):
    cmd = ["llvm-objdump", "-d", f"--section={section}", binary_path]
    try:
        output = subprocess.check_output(cmd, text=True)
    except subprocess.CalledProcessError:
        return {}
    lines = output.splitlines()
    symbol2stub = {}

    offset2symbol_lc = {int(k, 16): v for k, v in offset2symbol.items()}
    for line in lines:
        m = re.search(r'^ *([0-9a-fA-F]+):.*# *0x([0-9a-fA-F]+)', line)
        if m:
            stub_addr = m.group(1).lower()
            got_addr = int(m.group(2), 16)
            symbol = offset2symbol_lc.get(got_addr)
            if symbol and symbol not in symbol2stub:
                # ACCOUNT FOR ENDBR64 -> -4
                symbol2stub[symbol] = hex(int(stub_addr, 16)-4)
    return symbol2stub

def main(binary):
    offset2symbol, symbol2offset = parse_rela_plt(binary)
    symbol2stub = parse_plt_section(binary, ".fakeplt.sec", offset2symbol)
    symbol2stub_plt = parse_plt_section(binary, ".plt.sec", offset2symbol)
    for symbol, plt_addr in symbol2stub_plt.items():
        if symbol not in symbol2stub:
            symbol2stub[symbol] = hex(int(plt_addr, 16))
        elif symbol in symbol2stub and ("BothPLTs_" + symbol) not in symbol2stub:
                print("WTF ", symbol, hex(int(plt_addr, 16)))
                symbol2stub["BothPLTs_" + symbol] = hex(int(plt_addr, 16))

    #print("offset2symbol =", offset2symbol)
    #print("symbol2offset =", symbol2offset)
    print("symbol2pltstub =", symbol2stub)
    return symbol2stub


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <binary>")
        sys.exit(1)

    binary = sys.argv[1]
    main(binary)