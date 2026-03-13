# Lighttpd

Follow the steps:

- Clone `wrk` repo. The `master` branch is used.
    ```bash
    git clone https://github.com/wg/wrk.git
    cd wrk
    make -j8
    ```
- Inside `wrk` place the script `lighttpd.py`.
- In a directory of your choice in which **you have placed both the mitigated and the unmitigated versions** of `lighttpd`, place also the `lighttpd.conf`.
- Run:
    ```bash
    mkdir server
    sed -i "s|^server.document-root = .*|server.document-root = \"$(pwd)/server\"|" lighttpd.conf
    truncate -s 20480 ./server/test20k
    ```
- After the above, do (from the directory you have placed the lighttpd binaries):
    ```bash
    taskset -c 0-3 ./lighttpd -D -f ./lighttpd.conf
    ```
    This makes the server accepting connections.
- From the `wrk` directory execute `python3 lighttpd.py`. Currently the script makes `60` checks of 60 seconds each.
    If everything runs smoothly you should see (e.g., with `htop`) the 4 lighttpd cores saturated (cores 0-3), while the wrk cores almost saturated (cores 4-7).
- In the end to stop the `lighttpd` server, just Ctrl-C.