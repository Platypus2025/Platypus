#!/usr/bin/env python3
import subprocess
import re
from statistics import mean

# How many times to repeat the whole sequence
NUM_RUNS = 60

# Commands to benchmark
COMMANDS = [
    ("upload", "./ftpbench -u testuser -p platypus -H 127.0.0.1 -P 2121 -b concurrence -b upload"),
    ("concurrence", "./ftpbench -u testuser -p platypus -H 127.0.0.1 -P 2121 -b concurrence -n 500 -s 20M"),
    ("download", "./ftpbench -u testuser -p platypus -H 127.0.0.1 -P 2121 -b concurrence -b download"),
    ("transfer", "./ftpbench -u testuser -p platypus -H 127.0.0.1 -P 2121 -b concurrence -b transfer"),
]

# Regex to extract results
RESULT_RE = re.compile(r"^(.*?)(\d+(?:\.\d+)?)\s*(MB/sec|secs)?$")

# Store results: {command_name: {label: [values]}}
results = {}

def run_command(cmd):
    proc = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return proc.stdout.strip().splitlines()

def parse_output(lines, cmd_name):
    """Parse ftpbench output and store per-command results"""
    for line in lines:
        match = RESULT_RE.match(line.strip())
        if match:
            label, value, unit = match.groups()
            label = label.strip()
            value = float(value)
            unit = unit or ""
            key = f"{label} ({unit})".strip()
            results.setdefault(cmd_name, {}).setdefault(key, []).append(value)

def main():
    for run in range(1, NUM_RUNS + 1):
        print(f"\n########## RUN {run} ##########\n")
        for cmd_name, cmd in COMMANDS:
            print(f"== Output for: {cmd} ==")
            lines = run_command(cmd)
            print("\n".join(lines))
            parse_output(lines, cmd_name)
            print()

    print("\n================ FINAL SUMMARY ================\n")
    for cmd_name, metrics in results.items():
        print(f"--- {cmd_name.upper()} ---")
        for label, values in metrics.items():
            avg = mean(values)
            print(f"{label} -> Average: {avg:.2f} (from {len(values)} samples)")
        print()

if __name__ == "__main__":
    main()