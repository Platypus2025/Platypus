# Nginx

Follow the steps:

- Clone `wrk` repo. The `master` branch is used.
    ```bash
    git clone https://github.com/wg/wrk.git
    cd wrk
    make -j8
    ```
- Inside `wrk` place the script `nginx.py`.
- In a directory of your choice in which **you have placed both the mitigated and the unmitigated versions** of `nginx` run the `set.sh`. You need to place the shell script int he directory as well.
- Place the `nginx.conf` in the created `server` folder.
- After the above, do (from the directory you have placed the nginx binaries):
    ```bash
    ./nginx -c $(pwd)/server/nginx.conf -p $(pwd)/server/
    ```
    This makes the server accepting connections.
- From the `wrk` directory execute `python3 nginx.py`. Currently the script makes `60` checks of 60 seconds each.
    If everything runs smoothly you should see (e.g., with `htop`) the 4 nginx cores saturated (cores 0-3), while the wrk cores almost saturated (cores 4-7).
- In the end to stop the `nginx` server from running execute in the same dir as previously:
    ```bash
    ./nginx -s stop -c $(pwd)/server/nginx.conf -p $(pwd)/server/
    ```
