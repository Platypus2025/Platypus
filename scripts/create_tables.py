import sys
import subprocess
import re

def get_ptr_size(elf):
    out = subprocess.check_output(['llvm-readelf', '-h', elf], text=True)
    if 'Class:                             ELF64' in out:
        return 8
    elif 'Class:                             ELF32' in out:
        return 4
    else:
        raise Exception("Unknown ELF class")

def parse_section_info(elf, secname):
    out = subprocess.check_output(['llvm-readelf', '-S', elf], text=True)
    pat = re.compile(
        rf'\[\s*\d+\]\s+{re.escape(secname)}\s+\S+\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)'
    )
    for line in out.splitlines():
        m = pat.search(line)
        if m:
            vma = int(m.group(1), 16)
            size = int(m.group(3), 16)
            fileoff = int(m.group(2), 16)
            return vma, size, fileoff
    return None

def get_reloc_map(elf):
    out = subprocess.check_output(['llvm-readelf', '-rW', elf], text=True)
    relocs = {}
    for line in out.splitlines():
        columns = line.strip().split()
        if not columns or not re.match(r'[0-9a-fA-F]{8,16}', columns[0]):
            continue
        addr = int(columns[0], 16)
        relocs[addr] = columns
    return relocs

def read_array_entries(elf, fileoff, nentries, ptr_size):
    with open(elf, "rb") as f:
        f.seek(fileoff)
        data = f.read(nentries * ptr_size)
    addrs = []
    for i in range(nentries):
        chunk = data[i*ptr_size : (i+1)*ptr_size]
        addr = int.from_bytes(chunk, "little")
        addrs.append(addr)
    return addrs

def dedup_preserve_order(seq):
    seen = set()
    out = []
    for item in seq:
        if item not in seen:
            out.append(item)
            seen.add(item)
    return out

def main(elf, tag):
    ptr_size = get_ptr_size(elf)
    reloc_map = get_reloc_map(elf)
    init_stuff = []
    fini_stuff = []

    # .init_array entries
    sec = '.init_array'
    secinfo = parse_section_info(elf, sec)
    if secinfo:
        sec_addr, sec_size, sec_fileoff = secinfo
        nentries = sec_size // ptr_size
        print(f"{sec}: addr=0x{sec_addr:x}, entries={nentries}")
        file_addrs = read_array_entries(elf, sec_fileoff, nentries, ptr_size)
        for i in range(nentries):
            entry_addr = sec_addr + i * ptr_size
            if entry_addr in reloc_map:
                columns = reloc_map[entry_addr]
                if "RELATIVE" in columns[2] and len(columns) >= 4:
                    function_addr = int(columns[-1], 16)
                    print(f"  Entry {i}: function address=0x{function_addr:x} (reloc)")
                    init_stuff.append(function_addr)
                else:
                    print(f"  Entry {i}: not RELATIVE relocation ({' '.join(columns)})")
            else:
                print(f"  Entry {i}: no relocation")
            addr = file_addrs[i]
            if addr != 0:
                print(f"  Entry {i}: direct address=0x{addr:x} (file)")
                init_stuff.append(addr)
    else:
        print(f"{sec} does not exist")

    # .fini_array entries
    sec = '.fini_array'
    secinfo = parse_section_info(elf, sec)
    if secinfo:
        sec_addr, sec_size, sec_fileoff = secinfo
        nentries = sec_size // ptr_size
        print(f"{sec}: addr=0x{sec_addr:x}, entries={nentries}")
        file_addrs = read_array_entries(elf, sec_fileoff, nentries, ptr_size)
        for i in range(nentries):
            entry_addr = sec_addr + i * ptr_size
            if entry_addr in reloc_map:
                columns = reloc_map[entry_addr]
                if "RELATIVE" in columns[2] and len(columns) >= 4:
                    function_addr = int(columns[-1], 16)
                    print(f"  Entry {i}: function address=0x{function_addr:x} (reloc)")
                    fini_stuff.append(function_addr)
                else:
                    print(f"  Entry {i}: not RELATIVE relocation ({' '.join(columns)})")
            else:
                print(f"  Entry {i}: no relocation")
            addr = file_addrs[i]
            if addr != 0:
                print(f"  Entry {i}: direct address=0x{addr:x} (file)")
                fini_stuff.append(addr)
    else:
        print(f"{sec} does not exist")

    secinfo = parse_section_info(elf, '.init')
    if secinfo:
        vma, size, fileoff = secinfo
        print(f".init section exists, address=0x{vma:x}")
        init_stuff.append(vma)

    secinfo = parse_section_info(elf, '.fini')
    if secinfo:
        vma, size, fileoff = secinfo
        print(f".fini section exists, address=0x{vma:x}")
        fini_stuff.append(vma)

    init_dedup = dedup_preserve_order(init_stuff)
    fini_dedup = dedup_preserve_order(fini_stuff)
    print("\n.init & .init_array:", [tag + '_' + hex(addr) for addr in init_dedup])
    print(".fini & .fini_array:", [tag + '_' + hex(addr) for addr in fini_dedup])
    return (
        [tag + '_' + hex(addr) for addr in init_dedup],
        [tag + '_' + hex(addr) for addr in fini_dedup]
    )

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <elf> <tag>")
        sys.exit(1)
    elf = sys.argv[1]
    init_addrs, fini_addrs = main(elf, sys.argv[2])