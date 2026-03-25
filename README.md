# PlaTypus
Artifact Repository for the paper *PLATYPUS: Restricting Cross-Module Transitions to Mitigate Code-Reuse Attacks* submitted in S&P 2026 cycle 2

### Overview
* *llvm-platypus* folder contains only the files that modified from the official LLVM (20.1.0) toolchain due to size limitations of the Anonymous GitHub
* *glibc-platypus* folder contains the glibc used in the paper during evaluation
* *llvm-passes* folder contains the code of the three passes being used in paper. Specifically *FindDynsym* and *LogStructs* are used during the symbol gathering process (see section 3.4.2 in the paper). *BitMasks* is the pass instrumenting the code with our masking mechanism.
* In *scripts* folder there are various python scripts used mainly for creating the callback tables and the special functions linked with the main binary (see 3.4.3).

All the above require our modified LLVM compiler and linker to have been built, since they leverage them.


### Benchmarks

We provide the necessary benchmarks for evaluating the performance of the tool. For this purpose:

* The *libraries* folder contains subfolders for the different libraries we used. Note that we separate `glibc` from these libraries due to the fundamental role of `libc`. Each subfolder contains the necessary files to facilitate the automated compilation of the respective DSO (from downloading the source code to the final compilation). The most important files are the `compile.sh` scripts, which define all the steps required for the compilation process of each DSO. **Currently, the `compile.sh` scripts are designed to be executed through the `build_all.sh` script. To run them separately, specific environment variables need to be exported.**
* The *binaries* folder contains subfolders for the benchmarks used in the performance evaluation. The same structure as in libraries is followed. Each `compile.sh` script defines the steps required to build the corresponding main binary (from downloading the source code to the final compilation).
* The *Evaluation* folder contains detailed instructions on how to perform the performance evaluation for each of the previously mentioned components.

### How to use

We suggest executing the `build_all.sh` script, which performs all steps except for the evaluation. More specifically, the script is responsible for:
1. Downloading the LLVM toolchain and compiling the modified LLVM compiler and linker
2. Building the three LLVM compiler passes used by our tool
3. Compiling `glibc` both with and without PlaTypus instrumentation
4. Compiling every library under the *libraries* folder
5. Compile every main binary in the *binaries* fodler


### Prerequisites

The build pipeline depends on the following:

```
build-essential
git
cmake (>= 3.20)
ninja-build
python3 (tested with Python 3.12.3)
clang-20
libclang-20-dev (required by scripts/annotation.py)
python3-clang-20 (Python bindings for libclang)
libssl-dev
zlib1g-dev
bear
```

You can install them on your Ubuntu host with:

```
sudo apt update
sudo apt install -y \
    git \
    cmake \
    ninja-build \
    build-essential \
    python3 \
    clang-20 \
    libclang-20-dev \
    python3-clang-20 \
    libssl-dev \
    zlib1g-dev \
    bear
```


### Important Information

Currently, the script does not build the same LLVM toolchain as the one used by Platypus without the modifications. This happens because all our tests were performed on Ubuntu 24.04.1 machines, where the Clang toolchain version 20.1.2 can be installed directly from the system repositories using: `sudo apt install clang-20`. Therefore, we strongly recommend testing everything on an Ubuntu 24.04.1 host. Otherwise, the user can modify the script in the llvm-platypus folder so that it builds the toolchain from scratch without applying the diff.

To demonstrate the functionality of PlaTypus in integrating both instrumented and uninstrumented code, we intentionally do not instrument some of the DSOs used by the binaries. Specifically, these include several `NSS` libraries used by `bftpd` and `redis`, some C++ runtime libraries used by `redis`, and the `libm` DSO. For this purpose, we copy the corresponding system libraries into the directories containing the instrumented (and uninstrumented) libraries.
Since the exact set of installed libraries may vary depending on the system configuration, **we recommend that the user verify that the following paths are not empty:**
```bash
/lib/x86_64-linux-gnu/libcap.so*
/lib/x86_64-linux-gnu/libnss_sss.so*
/lib/x86_64-linux-gnu/libnss_systemd.so*
/lib/x86_64-linux-gnu/libnss_mdns4_minimal.so*
/lib/x86_64-linux-gnu/libstdc++.so*
/lib/x86_64-linux-gnu/libgcc_s.so*
/lib/x86_64-linux-gnu/libm.so*
```
If any of these paths is empty, the library may exist in a different location on the system. However, they are required for the binaries to function properly.

Additionally, `scripts/annotation.py` assumes that the file `/usr/lib/llvm-20/lib/libclang-20.so.1` exists on the system. Although this should be the case after `sudo apt install libclang-20-dev python3-clang-20`, please verify it beforehand, as the path is currently hardcoded in the Python script.