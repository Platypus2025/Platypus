# Compiling Libraries

First of all, the following should already be built:
1. The modified Clang compiler
2. The modified glibc
3. The 3 llvm passes

**Important**: Every library should rebuild the `BitMasks` pass for 2 reasons:
1. The way the pass understands which structs to instrument with the extended masking is by reading a `.cpp` file from a certain place. Every library should update this file (typically called here as `dso_callbacks.cpp`).
2. Every library should have their own symbol for their callback table.
Right now this symbol needs to be changed manually to the specified name, thus necessitating the recompilation of the `BitMasks` pass.

For this reason we provide in every library a copy of the `BitMasks.cpp` with the correct symbol name. The above needs to be automated in the future.



If so compiling a library comes down to the following:

- Create a `compile.json` file, for example by using `bear` (without Platypus).
This is needed in order to avoid annotating files that are definitely not necessary.
- Execute the `annotate.sh` script inside the directory.
This scripts annotates the callback getters (CBGs in paper) in the `.c` files used during the compilation.
If necessary a user can update the script so that it annotates also `.h` files.
- We run the first pass `FindDynSym`.
An example regarding `libcrypto` is:
    ```bash
    LOGFILE_PATH="$PWD/dynsym_crypto.log" make libcrypto.so CC="${PLATYPUS_CLANG}" CFLAGS="-O3 -g -fPIC -fpass-plugin=${DYNSYM_PLUGIN}" LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" -j1
    ```
    `dynsym.log` stores information necessary about the callbacks used by this DSO. This is important later when for a given binary we want to build the callback tables (per DSO).
    **Notice** that the previous instruction assumes that the `Makefile` enables the compialtion of the given target. If this is not the case then the user should apply the instruction only to the files necessary for the wanted target. Otherwise `dynsym.log` may be characterized by *noise*.
- We remove the previously compiled files and apply the second pass `LogStructs`.
    ```bash
    LOGFILE_PATH=$PWD/sym_crypto.log make libcrypto.so CC="${PLATYPUS_CLANG}" CFLAGS="-O3 -g -fpass-plugin=${STRUCT_PLUGIN}" LDFLAGS="-fuse-ld=lld -Wl,--allow-shlib-undefined" -j8
    ```
    This pass logs necessary information regarding structs with callbacks and also about CBGs.
- After this step it is necessary to extract from the `sym.log` the structs which need to be instrumented.
This is achieved with the `parse_structs.py`. Note that this script also logs possible global function pointers that need to be instrumented. The result of this script is passed into the `dso_callbacks.cpp`. This is not automated yet, so needs to be made manually. The `dso_callbacks.cpp` has the following layout:
    ```bash
    #pragma once
    #include <unordered_set>
    #include <string>

    extern const std::unordered_set<std::string> dso_callbacks = {
    };

    extern const std::unordered_set<std::string> struct_names = {
    };

    extern const std::unordered_set<std::string> global_names = {
    };
    ```
    In `struct_names` we fill the name of the logged structs, while in the `global_names` the name of the logged function pointers. `dso_callbacks` can be left untouched.
- We remove the previously compiled files and apply the third pass `BitMasks`.
This pass is eventually the one instrumenting the source code with the masking.
    ```bash
    PROTECT_JMP=True \
    make libcrypto.so \
    CC=${PLATYPUS_CLANG} \
    CFLAGS="-fPIC -g \
            -include ${PWD}/masks_crypto.h \
            -O3 \
            -fcf-protection=full \
            -fpass-plugin=${MASK_PLUGIN}" \
    LDFLAGS="${LD_LLD} \
            -rdynamic \
            -Wl,-z,relro,-z,now \
            -Wl,--dynamic-linker=${DYNAMIC_LINKER} \
            -Wl,--strip-debug \
            -Wl,--allow-shlib-undefined \
            -Wl,--version-script=${PWD}/exports_crypto.map \
            -Wl,--callb_getter=${PWD}/a_crypto.txt" \
    -j8
    ```
    `DYNAMIC_LINKER` is the loader of the instrumented glibc. `LD_LLD` is the modified Platypus linker.
    The linker flag `callb_getter` allows Platypus to instrument the PLT stubs of functions which accept callbacks (see §5: Arguments to CBGs). It can or not be applied depending on the user. We will show later how the file should be created.
    `masks_crypto.h` is just a header file declaring the 2 bitmask relocations plus an exported symbol (here `CRYPTO`) that should point to the start address of the DSO. This is achieved during linking. For example in libcrypto:
    ```bash
    ➜  openssl-3.2.1 cat masks_crypto.h
    extern long long int _DYNAMIC[];
    long long int * CRYPTO __attribute__((visibility("default"),weak)) = _DYNAMIC;
    extern long long int or_mask __attribute__((visibility("hidden")));
    ```
    `exports_crypto.map` is a map which we use in order to export the symbol which points to the start address of the library. This is necessary for some of our relocations. **Note** that the necessity of this depends each time on the library's `Makefile`. Some export them by defualt, some others not. Hacking through the `Makefile` should always be a solution however many times this is not trivial. Also note that some libraries create the symbol map long before calling `make` so a user can modify this map as well. In our example:
    ```bash
    ➜  openssl-3.2.1 cat exports_crypto.map
    {
    global:
        CRYPTO*;
    local:
        *;
    };
    ```
- After this we run the `callback_parser.py` script in which we give a lot of input files.
    ```bash
    python3 ~/path/to/platypus/scripts/callback_parser.py \
    libraries_crypto.json \
    dynsym_crypto.log \
    libcrypto.so.3 \
    0 \
    CRYPTO \
    reachable_structs \
    sym_crypto.log
    ```
    Here `libraries_crypto.json` is a file containing the dependencies in different libraries in a json file.
    ```bash
    ➜  openssl-3.2.1 cat libraries_crypto.json
    {
        "/path/to/platypus/libc.so.6":                   "LIBC",
        "path/to/platypus/ld-linux-x86-64.so.2":        "LD"
    }
    ```
    `0` denotes that this is a library and not an application.
    `CRYPTO` should always match the name of the symbol which points to the start of the DSO.
    `reachable_structs` is a file in which we can store whether the DSO uses structs of the same datatype which are exported or defined in a dependency DSO.

    As a result we see something like:
    ```bash
    Item to insert in callb_getters: qsort, lib: /opt/clang_glibc/lib/libc.so.6, positions: {'3'}
    {'ex_callback_compare', 'der_cmp', 'compare_params', 'do_all_sorted_cmp'}
    Found ex_callback_compare in: libcrypto.so.3 0x2e6cc0
    Found der_cmp in: libcrypto.so.3 0x1c1890
    Found compare_params in: libcrypto.so.3 0x2f0760
    Found do_all_sorted_cmp in: libcrypto.so.3 0x3b1020

    Item to insert in callb_getters: makecontext, lib: /opt/clang_glibc/lib/libc.so.6, positions: {'1'}
    {'async_start_func'}
    Found async_start_func in: libcrypto.so.3 0x1c51a0

    Item to insert in callb_getters: dladdr, lib: /opt/clang_glibc/lib/libc.so.6, positions: {'0'}
    {'dlfcn_pathbyaddr'}
    Found dlfcn_pathbyaddr in: libcrypto.so.3 0x239510

    Item to insert in callb_getters: __cxa_atexit, lib: /opt/clang_glibc/lib/libc.so.6, positions: {'0'}
    {'OPENSSL_cleanup'}
    Found OPENSSL_cleanup in: libcrypto.so.3 0x2e6fa0

    Item to insert in callb_getters: pthread_create, lib: /opt/clang_glibc/lib/libc.so.6, positions: {'2'}
    {'thread_start_thunk'}
    Found thread_start_thunk in: libcrypto.so.3 0x4154d0

    Item to insert in callb_getters: sigaction, lib: /opt/clang_glibc/lib/libc.so.6, positions: {'1'}
    {'recsig'}
    Found recsig in: libcrypto.so.3 0x41e1a0
    .init_array: addr=0x4b6688, entries=1
    Entry 0: function address=0x1970b0 (reloc)
    .fini_array: addr=0x4b6680, entries=1
    Entry 0: function address=0x197070 (reloc)
    .init section exists, address=0x4b5624
    .fini section exists, address=0x4b5644

    .init & .init_array: ['CRYPTO_0x1970b0', 'CRYPTO_0x4b5624']
    .fini & .fini_array: ['CRYPTO_0x197070', 'CRYPTO_0x4b5644']
    {'LIBC': ['CRYPTO_0x43c830', 'CRYPTO_0x2d1380', 'CRYPTO_0x4391f0', 'CRYPTO_0x43f790', 'CRYPTO_0x4465c0', 'CRYPTO_0x2c1260', 'CRYPTO_0x1b2740', 'CRYPTO_0x422500', 'CRYPTO_0x429bc0', 'CRYPTO_0x1b4f40', 'CRYPTO_0x3b2da0', 'CRYPTO_0x449400', 'CRYPTO_0x3cdf10', 'CRYPTO_0x1ba0d0', 'CRYPTO_0x1b9e10', 'CRYPTO_0x430450', 'CRYPTO_0x4210f0', 'CRYPTO_0x422140', 'CRYPTO_0x434580', 'CRYPTO_0x222cd0', 'CRYPTO_0x426820', 'CRYPTO_0x3b2db0', 'CRYPTO_0x2f5b10', 'CRYPTO_0x2e6cc0', 'CRYPTO_0x1c1890', 'CRYPTO_0x2f0760', 'CRYPTO_0x3b1020', 'CRYPTO_0x1c51a0', 'CRYPTO_0x2e7610', 'CRYPTO_0x29c980', 'CRYPTO_0x2e6ee0', 'CRYPTO_0x2e7680', 'CRYPTO_0x2e7670', 'CRYPTO_0x2e76b0', 'CRYPTO_0x3b0820', 'CRYPTO_0x2e7690', 'CRYPTO_0x2e3700', 'CRYPTO_0x2e75c0', 'CRYPTO_0x2e7530', 'CRYPTO_0x2e7650', 'CRYPTO_0x2e76e0', 'CRYPTO_0x223f00', 'CRYPTO_0x2e7fa0', 'CRYPTO_0x2e75f0', 'CRYPTO_0x3b2e90', 'CRYPTO_0x2e7720', 'CRYPTO_0x298310', 'CRYPTO_0x223f20', 'CRYPTO_0x2e75d0', 'CRYPTO_0x1cc310', 'CRYPTO_0x2e7710', 'CRYPTO_0x2e7660', 'CRYPTO_0x2e7600', 'CRYPTO_0x3b2540', 'CRYPTO_0x2e76c0', 'CRYPTO_0x3d0190', 'CRYPTO_0x414250', 'CRYPTO_0x29bb10', 'CRYPTO_0x2e7730', 'CRYPTO_0x41e520', 'CRYPTO_0x239510', 'CRYPTO_0x2e6fa0', 'CRYPTO_0x2e77a0', 'CRYPTO_0x4154d0', 'CRYPTO_0x41e1a0', 'CRYPTO_0x4280a0', 'CRYPTO_0x4280b0'], 'FINI': ['CRYPTO_0x197070', 'CRYPTO_0x4b5644'], 'THREADKEY': []}
    ```
    *Item to insert in callb_getters* defines functions that need to be added to the corresponding file passed as a linker flag (previously the `${PWD}/a_crypto.txt`). Its format should be:
    ```bash
    ➜  openssl-3.2.1 cat a_crypto.txt
    makecontext:(1)
    pthread_create:(2)
    __cxa_atexit:(0)
    dladdr:(0)
    ```
    Finally the dictionary
    ```bash
    {'LIBC': ['CRYPTO_0x43c830', 'CRYPTO_0x2d1380', 'CRYPTO_0x4391f0', 'CRYPTO_0x43f790', 'CRYPTO_0x4465c0', 'CRYPTO_0x2c1260', 'CRYPTO_0x1b2740', 'CRYPTO_0x422500', 'CRYPTO_0x429bc0', 'CRYPTO_0x1b4f40', 'CRYPTO_0x3b2da0', 'CRYPTO_0x449400', 'CRYPTO_0x3cdf10', 'CRYPTO_0x1ba0d0', 'CRYPTO_0x1b9e10', 'CRYPTO_0x430450', 'CRYPTO_0x4210f0', 'CRYPTO_0x422140', 'CRYPTO_0x434580', 'CRYPTO_0x222cd0', 'CRYPTO_0x426820', 'CRYPTO_0x3b2db0', 'CRYPTO_0x2f5b10', 'CRYPTO_0x2e6cc0', 'CRYPTO_0x1c1890', 'CRYPTO_0x2f0760', 'CRYPTO_0x3b1020', 'CRYPTO_0x1c51a0', 'CRYPTO_0x2e7610', 'CRYPTO_0x29c980', 'CRYPTO_0x2e6ee0', 'CRYPTO_0x2e7680', 'CRYPTO_0x2e7670', 'CRYPTO_0x2e76b0', 'CRYPTO_0x3b0820', 'CRYPTO_0x2e7690', 'CRYPTO_0x2e3700', 'CRYPTO_0x2e75c0', 'CRYPTO_0x2e7530', 'CRYPTO_0x2e7650', 'CRYPTO_0x2e76e0', 'CRYPTO_0x223f00', 'CRYPTO_0x2e7fa0', 'CRYPTO_0x2e75f0', 'CRYPTO_0x3b2e90', 'CRYPTO_0x2e7720', 'CRYPTO_0x298310', 'CRYPTO_0x223f20', 'CRYPTO_0x2e75d0', 'CRYPTO_0x1cc310', 'CRYPTO_0x2e7710', 'CRYPTO_0x2e7660', 'CRYPTO_0x2e7600', 'CRYPTO_0x3b2540', 'CRYPTO_0x2e76c0', 'CRYPTO_0x3d0190', 'CRYPTO_0x414250', 'CRYPTO_0x29bb10', 'CRYPTO_0x2e7730', 'CRYPTO_0x41e520', 'CRYPTO_0x239510', 'CRYPTO_0x2e6fa0', 'CRYPTO_0x2e77a0', 'CRYPTO_0x4154d0', 'CRYPTO_0x41e1a0', 'CRYPTO_0x4280a0', 'CRYPTO_0x4280b0'], 'FINI': ['CRYPTO_0x197070', 'CRYPTO_0x4b5644'], 'THREADKEY': []}
    ```
    contains all the possible callbacks that may be called from the libraries which are denoted as keys (here only `LIBC`). `FINI` and `THREADKEY` are special cases of callbacks which should only target a specific set of pointers (see §4.2.2).
    This dictionary should be stored for every library since it will be used by every program that depends on it (the specific DSO).

- The creation of the `${PWD}/a_crypto.txt` and the file storing the dictionary is created using the script `parse_output.py` applied to a file storing the output of the `callback_parser.py` script.
    ```bash
    python3 parse_output.py callback_parse_output_crypto a_crypto.txt header_crypto.txt
    ```
    The last 2 arguments specify the names of the 2 created files. Default names are `a.txt` and `header.txt`.


After all of the above a fast way to see if the library has been compiled and linked with PlaTypus is executing `llvm-readelf -rW` in the binary and searching for relocations like `ORMASK` and `ANDMASK`.
```bash

```

### Test cases

For the most important libraries used in our benchmarks we provide specific bash scripts automating the whole process.

**Note** that to enable automation we provide for each library in its folder the following:
1. If needed, the file which is the argument to `callb_getter` linker option (previously `a.txt`). This can also be verified by our `parse_output.py` script.
2. The `masks.h` file declaring the symbol pointing to the start of the DSO.
3. If needed an `exports.map` file which enforces that the symbol declared in `masks.h` is exported.
4. The `libraries.json` dict. Currently we assume that every library after compilation is stored under `/opt/platypus`.
5. The `dso_callbacks.cpp` file which holds information about the structs that need to be instrumented.

Ideally all the above should be automated.