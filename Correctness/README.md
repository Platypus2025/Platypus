## Correctness

In this folder, we provide instructions on how to evaluate the benchmarks for *correctness*.
By correctness, we mean running each benchmark with its provided test suite (typically bundled within the codebase) and verifying that no crashes occur.

This folder contains one script for each of the six binaries: sqlite, nginx, redis, bftpd, lighttpd, and memcached.
We did not combine everything into a single script because some cases produce a large number of logs, which would make checking the results less user-friendly.

Run each of the scripts **after you have successfully built everything using `build_all.sh`**.
Run them also **inside this directory**.

As you can observe, in most cases, before running `make check` (which is typically used to execute the test suite), we first clean the environment and compile everything without instrumentation. We then replace the binary required for correctness testing with the instrumented version.

This is done because otherwise the files used to exercise the test suite would also be instrumented with our mitigation, which is not desired.


### Important Information

You may notice that in some test suites, certain tests are skipped (e.g., in memcached or nginx).
This is not an error; it is related to how each binary was configured.
For example, for memcached we used the default configuration, while for nginx we enabled additional features such as SSL, gzip compression, and others.

For the `sqlite` binary, there is a small nuance. You need to install the following package:
```
sudo apt install tcl-dev
```
Afterwards, please verify that the file `/usr/lib/x86_64-linux-gnu/tcl8.6/tclConfig.sh` exists on your system. This file is required for the SQLite test suite. If it exists, then before running the SQLite tests, export it as follows:
```
TCL_CONFIG_SH=/usr/lib/x86_64-linux-gnu/tcl8.6/tclConfig.sh
```