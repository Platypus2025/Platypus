import subprocess
import re

NUM_RUNS = 60
CMD = [
    "taskset", "-c", "4-5",
    "./memtier_benchmark",
    "--server=127.0.0.1", "--port=6379",
    "--protocol=redis",
    "--threads=2",
    "--clients=128",
    "--ratio=1:10",
    "--data-size=32",
    "--test-time=60"
]

totals_ops = []

for i in range(NUM_RUNS):
    print(f"Run {i+1}/{NUM_RUNS}...")
    try:
        proc = subprocess.run(CMD, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        m = re.search(r"Totals\s+([0-9.]+)", proc.stdout)
        if m:
            value = float(m.group(1))
            totals_ops.append(value)
            print(f"  Totals Ops/sec: {value}")
        else:
            print("  ERROR: Could not find Totals Ops/sec in output!")
            totals_ops.append(None)
    except Exception as e:
        print(f"  ERROR: Command failed with {e}")
        totals_ops.append(None)

# Remove any runs that failed (None)
clean_totals = [val for val in totals_ops if val is not None]
average = sum(clean_totals)/len(clean_totals) if clean_totals else float('nan')

print("\nAll Totals Ops/sec values:")
print(clean_totals)
print(f"\nAverage Totals Ops/sec: {average:.2f}")