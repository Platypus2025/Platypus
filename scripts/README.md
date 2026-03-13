# PlaTypus Scripts

This folder contains the scripts required by our tool. The most important ones are the following.

- `annotate.sh` is a wrapper around `annotation.py`. Its purpose is to annotate Callback Getters (CBGs) in code. For the script to work, a `compile_commands.json` file must be present. This can be generated, for example, with `bear -- make ...`.
- `callback_parser.py` is the core orchestrator of creating the *callback tables* for a binary and its DSOs. It requires the following arguments (in order):
    1. A JSON file, usually named `libraries.json`, whose keys are the paths of the DSOs on which a binary depends and whose values are *tag* names for those DSOs (for example, `LIBC` for `libc`, `CRYPTO` for `libcrypto`, etc.). Each library exports its tag, and our linker makes it point to the start address of the DSO. Applications also export a tag; for the main binary, we use `MB`. It is important to use the same tag consistently for a given DSO if we want to create a directory of instrumented libraries that can be reused by applications.
    2. A log file containing the necessary information about callbacks for a given DSO or application. For example, it records callbacks and the functions (CBGs) to which they are passed as arguments. This file is produced by compiling the target with the `FindDynSym` pass.
    3. The path to the binary to which `callback_parser.py` is applied. This can be either a DSO or a main binary.
    4. A Boolean value. If it is `0`, then the binary given in the previous argument is a DSO. If it is `1`, then it is a main binary.
    5. The binary’s tag. For example, when applied to `libssl.so`, we use `LSSL`. If it is applied to a main binary, we use `MB`. **Although users may choose different tags for libraries, the script currently assumes that the tag MB is always used for main binaries.**
    6. A special file that we call `reachable_structs`. There are cases, such as `nginx`, in which a binary uses exactly the same structure types as those defined in one of its dependent DSOs. We must be aware of this because, in such cases, callbacks can often be passed through these structures, and therefore the callback tables of the dependent DSOs should account for this.

        The file follows a simple format: the name of the dependent DSO, followed by an underscore, followed by the names of the relevant structure types. For example, in `nginx` it looks like this:
        ```bash
        ~/Documents/artifacts/nginx-1.28.0$ cat reachable_structs
        LIBZ:internal_state,z_stream_s  
        ```
        Currently, this must be produced manually by inspecting the structures used by a binary and identifying collisions with the structures of its dependent DSOs. See also the explanation for the script `parse_structs.py` later in this README   .
    7. A log file containing information about the structures of the instrumented binary (the one used in argument #3). Note that this is different from the previous log file. This file is produced by the `LogStructs` pass.

    This script ultimately produces a file, typically named `header.txt`, that contains lines of dictionaries. Each dictionary has tags defining libraries as keys and lists of special constructs as values. Each construct has the form `TAG_hexnum`, where hexnum is the offset inside the library identified by `TAG`. An example for `libcrypto` is shown below:
    ```bash
    cat openssl-3.2.1/header_crypto.txt
    {'LIBC': ['CRYPTO_0x444d20', 'CRYPTO_0x3c5e80', 'CRYPTO_0x2e0e50', 'CRYPTO_0x306c90', 'CRYPTO_0x1bde00', 'CRYPTO_0x45cc50', 'CRYPTO_0x436270', 'CRYPTO_0x44df60', 'CRYPTO_0x3e0430', 'CRYPTO_0x436690', 'CRYPTO_0x454c50', 'CRYPTO_0x4519d0', 'CRYPTO_0x449140', 'CRYPTO_0x1b8ab0', 'CRYPTO_0x1bdb40', 'CRYPTO_0x43dfd0', 'CRYPTO_0x2284f0', 'CRYPTO_0x3c5e70', 'CRYPTO_0x435210', 'CRYPTO_0x1b6170', 'CRYPTO_0x2ce170', 'CRYPTO_0x43aa60', 'CRYPTO_0x45fb80', 'CRYPTO_0x1c5cb0', 'CRYPTO_0x2f7700', 'CRYPTO_0x3c4020', 'CRYPTO_0x301760', 'CRYPTO_0x1c9c80', 'CRYPTO_0x2f8190', 'CRYPTO_0x2f8bc0', 'CRYPTO_0x229970', 'CRYPTO_0x2f8140', 'CRYPTO_0x2f8170', 'CRYPTO_0x2a7b20', 'CRYPTO_0x2f8200', 'CRYPTO_0x229950', 'CRYPTO_0x2f8070', 'CRYPTO_0x2f8080', 'CRYPTO_0x2f8120', 'CRYPTO_0x427d10', 'CRYPTO_0x3c55d0', 'CRYPTO_0x2f8150', 'CRYPTO_0x1d1a40', 'CRYPTO_0x2f7940', 'CRYPTO_0x432620', 'CRYPTO_0x2f81b0', 'CRYPTO_0x2f80a0', 'CRYPTO_0x2f80b0', 'CRYPTO_0x2a2da0', 'CRYPTO_0x2f3d90', 'CRYPTO_0x2f80c0', 'CRYPTO_0x2f7fd0', 'CRYPTO_0x2f8240', 'CRYPTO_0x3c37a0', 'CRYPTO_0x2a6c90', 'CRYPTO_0x3c5f70', 'CRYPTO_0x2f8110', 'CRYPTO_0x2f81d0', 'CRYPTO_0x2f8220', 'CRYPTO_0x3e2730', 'CRYPTO_0x23f830', 'CRYPTO_0x2f7a00', 'CRYPTO_0x2f82e0', 'CRYPTO_0x429040', 'CRYPTO_0x432250', 'CRYPTO_0x43c410', 'CRYPTO_0x43c430'], 'FINI': ['CRYPTO_0x19a070', 'CRYPTO_0x4bdee4'], 'THREADKEY': []}
    ```
   For example, `CRYPTO_0x444d20` means that *libc should be allowed to call, as a callback, the function located in libcrypto at offset 0x444d20*. `FINI` and `THREADKEY` are 2 special tags related with 2 `FOP` dispatchers (see paper §4.2.2).\
    **Important**: For main binaries, the produced files look like the one above, except that they always begin with a dictionary that is usually empty. However, if there are cases in which callbacks are transferred between multiple DSOs (for example, `libssl` passes a received callback to `libcrypto`), we record this relation in the first dictionary. This currently has to be done manually. The script determines whether this is necessary and prints a message in *orange* when it is.
    ```bash
    ➜  openssl-3.2.1 python3 ~/callback_parser.py libraries_crypto.json dynsym_crypto.log libcrypto.so.3  0 CRYPTO reachable_structs sym_crypto.log 
    ...
    Transfer callback table to /path/to/instrumented/libc.so.6 cause of qsort.
    ...
    ```
    The message above means that `libcrypto` must transfer all of its callback table entries to the callback table of `libc`, because any of them may transitively reach `libc`. For example, this happens in `nginx`, whose produced file looks as follows:
    ```bash
    nginx-1.28.0$ cat header.txt
    {'CRYPTO':'LSSL', 'LIBC':'CRYPTO'}
    {'LIBC': ['MB_0x82b40', 'MB_0x103dd0', 'MB_0xe3070', 'MB_0xfec80', 'MB_0xfd950', 'MB_0x9b910', 'MB_0x116810', 'MB_0x118780', 'MB_0xe24b0', 'MB_0xd98e0', 'MB_0x5dbd0', 'MB_0x11ad8c'], 'LSSL': ['MB_0x8d7f0', 'MB_0x8e190', 'MB_0x8ec50', 'MB_0x91090', 'MB_0x916a0', 'MB_0x918e0', 'MB_0x920f0', 'MB_0x958f0', 'MB_0xa4590', 'MB_0x1120d0', 'MB_0x112330', 'MB_0xe5780', 'MB_0xa4b80', 'MB_0x112370'], 'CRYPTO': ['MB_0x11ae00', 'MB_0x11ade0', 'MB_0x11ae20', 'MB_0x8e1a0', 'MB_0x95100', 'MB_0x11ae40', 'MB_0x11ae60'], 'PCR': ['MB_0x987c0', 'MB_0x98780'], 'LIBZ': ['MB_0xac2c0', 'MB_0xac2d0', 'MB_0xc77f0', 'MB_0xc78c0', 'MB_0x1099b0', 'MB_0x1099c0'], 'FINI': ['MB_0x5db90', 'MB_0x11ada8'], 'THREADKEY': []}
    ...
    ...
    ```
- `parse_output.py` automates the creation of the `header.txt` by pasring the output of `callback_parser.py` and creating the `header.txt` and if the user wants also a file named `a.txt` containing the PLTs that should be instrumented with masking (see paper §5 Arguments to CBGs). This `a.txt` can then be recognized by our modified linker and instrument the corresponding PLTs. The information about which PLTs require this is emitted by `callback_parser.py`. If it is applied to a main binary then the last argument should be `bin`. The previous 2 are the names of the `a.txt` and `header.txt` respectively. If omitted then the default file names (`a.txt` and `header.txt`) are used.
- `create_header.py` accepts a file like `header.txt` and creates 2 files, the `mask.h` and `mask.c` which actually define the special constructs (`TAG_hexnum`) as symbols and contain the asm pads for every function that searches the callback tables (one per DSO). **This script only makes sense to be used with main binaries**. In the end the produced `mask.c` need to compiled with `clang -c mask.c -fcf-protection=full` and the `mask.o` is later linked to the main binary in the linker flags. Actually the trick here is to remove the target main binary and rebuild by adding `mask.o` in the `LDFLAGS`. This *does not* recompiles the files but links one more together.

    There are some special features for this script. In case of `dlsymed` symbols these are passed as arguments after the `header.txt` in the format `dsym_<sym_name>`. For example:
    ```bash
    create_header.py ./header_sort.txt dsym_MD5_Init dsym_MD5_Update dsym_MD5_Final
    ```
    Also in case of `NSS` symbols these are also passed as arguments under the special format `placeholder<i>_<sym_name>`. For example:
    ```bash
    create_header.py ./header.txt placeholder1__nss_systemd_getpwnam_r placeholder2__nss_sss_getpwnam_r dsym_MD5_Init
    ```
- `parse_structs.py` parses the file (typically `sym.log`) produced by the pass `LogStructs`. It outputs a `.cpp` file which is almost the `dso_callback.cpp` used by `BitMasks` pass. `dso_callback.cpp` should contain 3 `const std::unordered_set<std::string>`:
    ```cpp
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
    The usage of `dso_callbacks` has beed deprecated so it can always be left empty. `parse_structs.py` produces `struct_names`. For example in `libz`:
    ```bash
    ➜ python3 parse_structs.py sym.log

        #include <unordered_set>
        #include <string>

        extern const std::unordered_set<std::string> struct_names = {
            "internal_state",
            "z_stream_s",
        };
    ➜ 
    ```
    So a user can copy this for each binary to a `dso_callbacks.cpp` file and also add the remaining 2 sets. As we said `dso_callbacks` can remain empty. Filling `global_names` requires manual work. A user needs to search the file produced by `LogStructs` and include in `global_names` every entry which is not a struct. This can be used with the command `cat sym.log | grep -v "struct\."`. For example in `libcrypto` this produces:
    ```bash
    ➜  openssl-3.2.1 cat sym_crypto.log | grep -v "struct\."
    stack_alloc_impl
    stack_free_impl
    malloc_impl
    realloc_impl
    free_impl
    default_trust
    ```
    These 6 strings need to be placed in the `global_names`.

    **We remind again that after the creation of `dso_callbacks.cpp` file, it needs to be placed in the correct path (the one defined in the header of `BitMasks.cpp`). On top, the `BitMasks` pass needs to be recompiled.**