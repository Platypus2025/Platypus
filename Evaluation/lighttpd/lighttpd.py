import subprocess
import re

NUM_RUNS = 60
CMD = ["taskset", "-c", "4-7", "./wrk", "-t4", "-c400", "-d60s", "http://127.0.0.1:8080/test20k"]

results = []

for i in range(NUM_RUNS):
    print(f"Run {i+1}/{NUM_RUNS}...")
    # Run wrk
    proc = subprocess.run(CMD, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    # Find line with Requests/sec
    m = re.search(r"Requests/sec:\s+([\d\.]+)", proc.stdout)
    if m:
        value = float(m.group(1))
        results.append(value)
        print(f"  Requests/sec: {value}")
    else:
        print("  ERROR: Could not find Requests/sec in output.")
        print(proc.stdout)
        print(proc.stderr)
        results.append(None)  # Or skip, depending on desired robustness

# Filter out None if any run failed
clean_results = [v for v in results if v is not None]
average = sum(clean_results) / len(clean_results) if clean_results else float('nan')

print("\nResults array:")
print(clean_results)
print(f"\nAverage Requests/sec: {average:.2f}")