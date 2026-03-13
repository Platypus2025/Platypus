# Memcached

Follow the steps:

- Clone `memtier_benchmark` repo. The `master` branch is used.
    ```bash
    git clone https://github.com/redis/memtier_benchmark.git
    cd memtier_benchmark
    git checkout f3545b0f59ae21ad8b702aec9d15aacbccdbc41b
    autoreconf -ivf
    ./configure
    make
    ```
- Inside the `memtier_benchmark` dir place the script `memcached.py`.
- In a directory of your choice in which **you have placed both the mitigated and the unmitigated versions** of `memcached` run:
    ```bash
    taskset -c 0-3 ./memcached -p 11211 -m 1024 -t 4 -o hashpower=22
    ```
- From the `memtier_benchmark` directory execute `python3 memcached.py`. Currently the script makes `60` checks of 60 seconds each.
