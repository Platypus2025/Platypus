# Sqlite

To evaluate `sqlite` the `speedtest1` binary needs to be built inside the folder of sqlite. This can be done through our automated script which builds everything. Assuming `speedtest1` is ready (both the mitigated and the unmitigated version) follow the steps:

- Inside the folder with the source code of sqlite place the `speedtest1` binaries and the `sqlite.py` script.
- Execute `python3 sqlite.py` for both the mitigated and the unmitigated version.
