#!/usr/bin/env python3
import sys
import ast
from collections import defaultdict


def load_file(path):
    with open(path, "r") as f:
        lines = [line.strip() for line in f if line.strip()]
    return lines


def dfs_collect(key, rules, data, memo, visiting):
    if key in memo:
        return memo[key]

    if key in visiting:
        return set(data.get(key, set()))

    visiting.add(key)

    result = set(data.get(key, set()))

    if key in rules:
        src = rules[key]
        result |= dfs_collect(src, rules, data, memo, visiting)

    visiting.remove(key)
    memo[key] = result
    return result


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <input_file>")
        sys.exit(1)

    path = sys.argv[1]
    lines = load_file(path)

    if not lines:
        print("empty input file")
        sys.exit(1)

    rules = ast.literal_eval(lines[0])

    data = defaultdict(set)

    for line in lines[1:]:
        d = ast.literal_eval(line)
        for key, values in d.items():
            data[key].update(values)

    all_keys = set(data.keys()) | set(rules.keys()) | set(rules.values())

    memo = {}
    final_data = {}

    skip = {"FINI", "THREADKEY"}

    for key in sorted(all_keys):
        final_data[key] = dfs_collect(key, rules, data, memo, set())

    for key in sorted(final_data):
        if key in skip:
            continue
        print(f"{key} {len(final_data[key])}")


if __name__ == "__main__":
    main()