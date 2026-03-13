# Redis

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
- Inside the `memtier_benchmark` dir place the script `redis.py`.
- In a directory of your choice in which **you have placed both the mitigated and the unmitigated versions** of `redis-server` run:
    ```bash
    export LC_ALL=C
    export LANG=C
    taskset -c 3 ./redis-server --bind 127.0.0.1 --port 6379 --save "" --appendonly no
    ```
    The exports are needed just to verify that there are no locale issues.
- From the `memtier_benchmark` directory execute `python3 redis.py`. Currently the script makes `60` checks of 60 seconds each.
