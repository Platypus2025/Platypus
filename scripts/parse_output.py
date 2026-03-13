#!/usr/bin/env python3

import ast
import re
import sys
from pathlib import Path


ITEM_RE = re.compile(
    r"^Item to insert in callb_getters:\s*([^,]+),\s*lib:\s*([^,]+),\s*positions:\s*\{([^}]*)\}\s*$"
)


def extract_first_position(positions_str: str):
    nums = re.findall(r"\d+", positions_str)
    return nums[0] if nums else None


def parse_callb_getters(text: str):
    result = []

    for line in text.splitlines():
        m = ITEM_RE.match(line.strip())
        if not m:
            continue

        symbol_name = m.group(1).strip()
        positions_raw = m.group(3).strip()

        if symbol_name == "sigaction":
            continue

        first_pos = extract_first_position(positions_raw)
        if first_pos is None:
            continue

        result.append((symbol_name, first_pos))

    return result


def write_a_txt(pairs, output_path: Path):
    with output_path.open("w", encoding="utf-8") as f:
        for symbol_name, pos in pairs:
            f.write(f"{symbol_name}:({pos})\n")


def find_final_dictionary(text: str):
    lines = text.splitlines()

    for line in reversed(lines):
        stripped = line.strip()
        if not stripped.startswith("{"):
            continue
        if not (stripped.startswith("{") and stripped.endswith("}")):
            continue

        try:
            parsed = ast.literal_eval(stripped)
            if isinstance(parsed, dict):
                return parsed
        except Exception:
            continue

    return None


def write_header_txt(data: dict, output_path: Path, bin_mode: bool = False):
    with output_path.open("w", encoding="utf-8") as f:
        if bin_mode:
            f.write("{}\n")
        f.write(repr(data))
        f.write("\n")


def main():
    if len(sys.argv) < 2 or len(sys.argv) > 5:
        print(
            f"Usage: {sys.argv[0]} <input_log> [header_output_name] [a_output_name header_output_name] [bin]",
            file=sys.stderr,
        )
        sys.exit(1)

    args = sys.argv[1:]
    bin_mode = False

    if args and args[-1] == "bin":
        bin_mode = True
        args = args[:-1]

    if len(args) < 1 or len(args) > 3:
        print(
            f"Usage: {sys.argv[0]} <input_log> [header_output_name] [a_output_name header_output_name] [bin]",
            file=sys.stderr,
        )
        sys.exit(1)

    input_path = Path(args[0])

    write_a = True
    a_txt_path = Path("a.txt")
    header_txt_path = Path("header.txt")

    if len(args) == 2:
        # Only header output name given; do not create a.txt
        if args[1] == "bin":
            print("Error: 'bin' is reserved and cannot be used as a file name", file=sys.stderr)
            sys.exit(1)
        write_a = False
        header_txt_path = Path(args[1])

    elif len(args) == 3:
        # Both a output name and header output name given
        if args[1] == "bin" or args[2] == "bin":
            print("Error: 'bin' is reserved and cannot be used as a file name", file=sys.stderr)
            sys.exit(1)
        a_txt_path = Path(args[1])
        header_txt_path = Path(args[2])

    if not input_path.is_file():
        print(f"Error: input file not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    text = input_path.read_text(encoding="utf-8", errors="replace")

    if write_a:
        pairs = parse_callb_getters(text)
        write_a_txt(pairs, a_txt_path)
        print(f"Wrote {len(pairs)} entries to {a_txt_path}")

    final_dict = find_final_dictionary(text)
    if final_dict is None:
        print("Error: could not find final dictionary in input log", file=sys.stderr)
        sys.exit(1)

    write_header_txt(final_dict, header_txt_path, bin_mode=bin_mode)
    print(f"Wrote final dictionary to {header_txt_path}")


if __name__ == "__main__":
    main()
