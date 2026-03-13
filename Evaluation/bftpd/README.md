# Bftpd

Follow the steps:

- Clone `pyftpdlib` repo.
    ```bash
    git clone https://github.com/giampaolo/pyftpdlib.git
    cd pyftpdlib/scripts
    python3 -m pip install --user pyasynchat
    ```
- Inside `pyftpdlib/scripts` place the script `bftpd.py`.
- In a directory of your choice in which **you have placed both the mitigated and the unmitigated verions** of `bftpd`, place the `bftpd.conf` as well.


- Then run:
    ```bash
    sudo useradd -m testuser
    sudo passwd testuser # Set the password to platypus
    sudo mkdir -p /tmp/ftp-test
    sudo chown testuser:testuser /tmp/ftp-test
    ```
- Run the `bftpd` (either the mitigated or the unmitigated) like:
    ```bash
    sudo ./bftpd -d -c ./bftpd.conf
    ```
- On the `pyftpdlib/scripts` execute `python3 bftpd.py`. Currently there are 60 iterations being made.
- When finish terminate the `bftpd` process (requires `sudo`).
