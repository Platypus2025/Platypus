import subprocess
import time

times = []
n_runs = 60

for i in range(n_runs):
    print(f"Run {i+1}/{n_runs}...")
    start = time.time()
    # Pin subprocess to CPU core 3 with taskset (-c 3)
    subprocess.run(
        ["taskset", "-c", "3", "./speedtest1", ":memory:", "--size", "300"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    end = time.time()
    elapsed = end - start
    times.append(elapsed)
    print(f"  Run time: {elapsed:.3f} seconds")

# Print all times
print("\nAll run times:")
for i, t in enumerate(times, 1):
    print(f"Run {i}: {t:.3f} seconds")

# Compute and print the average
avg = sum(times) / len(times)
print(f"\nAverage run time: {avg:.3f} seconds")