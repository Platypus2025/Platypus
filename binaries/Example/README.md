## How to compile main binaries with PLaTypus

Although the process is mostly the same as for the libraries, compiling the main binaries involves a few additional steps.
We will go through each of them using the demo C files in these directories. You can then follow the same process for any other main binary you want to compile.

First, add the modified compiler to your `PATH` for easier use. Also, export the root of the repository using **an absolute path**.
```bash
export PATH="../../llvm-project/build/bin:$PATH"
export ROOT_DIR=/path/to/Platypus # CHANGE THIS
```

First, apply the `FindDynSym` pass to the target files, here `main.c` and `prog2.c`.
```bash
LOGFILE_PATH=$PWD/dynsym.log bear -- clang -g -fcf-protection=full -O0 \
 -fpass-plugin=../../llvm-passes/FindDynSym/build/libDynsym.so -o main main.c prog2.c
```
This collects the necessary symbols, mainly those related to callbacks. In our example, you should see the following:
```bash
➜  cat dynsym.log
Possible Struct Callback global: calloc : struct.<unknown>
func3: puts at arg index 0
func3: puts at arg index 0
```

Notice that this step already compiles a binary, but it is not the final instrumented one. Before applying the next pass, we need to clean the generated files. In this case, simply remove the produced binary, `main`. The two symbol-gathering passes could be inserted directly into the compilation pipeline. However, the current version of PLaTypus does not support this. Therefore, we need to apply them sequentially: compile the binary, gather the symbols, and then clean the compiled files.

Before applying the second pass, it is important to annotate the source code you want to instrument. For this, a `compile_commands.json` file must be available (for example, generated with `bear`), so that the annotation is applied only to the files actually used during compilation. This step is especially important in large codebases with complex `Makefiles`, where building a specific target may not be straightforward. To annotate execute:
```bash
../../scripts/annotate.sh annon.log ./
```

The logs should be:
```
Annotating ./prog2.c ...
[INFO] found: func3 at /home/pitogyro/Downloads/Platypus/binaries/Example/prog2.c:12
[INFO] Found 1 matching functions in prog2.c
[NOTE] Will annotate func3 at line 12 in prog2.c
[INFO] Annotated 1 functions in prog2.c
Annotating ./main.c ...
[INFO] Found 0 matching functions in main.c
[INFO] No functions with function pointer arguments found in main.c
```
As shown, `func3` has been annotated. You can verify this by checking the file `prog2.c`.

Now apply the `LogStruct` pass.
```bash
rm main
LOGFILE_PATH=$PWD/sym.log clang -O0 -g -fcf-protection=full -fpass-plugin=../../llvm-passes/LogStructs/build/libLogstructs.so -fuse-ld=lld -o main main.c prog2.c
```

Now we are ready to instrument the code. Execute the following:
```bash
rm main
PROTECT_JMP=True clang -g -O0 -fcf-protection=full -fpass-plugin=../../llvm-passes/BitMasks/build/libBitmask.so -fuse-ld=lld -Wl,-z,relro,-z,now -Wl,--dynamic-linker=$ROOT_DIR/libraries/instrumented_libs/ld-linux-x86-64.so.2 -Wl,-rpath,$ROOT_DIR/libraries/instrumented_libs -Wl,--allow-shlib-undefined -rdynamic -o main main.c prog2.c
```

The above command instruments the binary. However, it is still not ready, since the required callback table stubs have not yet been emitted. To gather the necessary information, initially execute:

```bash
./create_libs.sh
touch reachable_structs
[ -f sym.log ] || touch sym.log
```
This creates the `libraries.json` file, which contains the library dependencies of the corresponding binary. Note that, for different binaries, the user must provide the correct libraries each time.
The second instruction creates a file in which struct data types from different DSOs used by the main binary should be listed (for the required format, see the `README` in the `scripts` folder). In this demo, there are no such structs, so the file remains empty.
The last command ensures that the `sym.log` file exists, even if it is empty. Since this demo is simple, there are no structs that require special instrumentation with extended masking, and therefore the pass does not create this file. Nevertheless, its presence **is required** by the following scripts, so in such cases we create an empty one.

After the above, execute the following sequence of commands:

```bash
python3 ../../scripts/callback_parser.py \
  ./libraries.json \
  dynsym.log \
  main \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt
```
```bash
python3 ../../scripts/parse_output.py output.txt header.txt bin
```
```bash
cat "${ROOT_DIR}/header.txt" >> header.txt
```
These three commands create the necessary metadata files to be parsed so that the correct stub files related to callback tables can be generated.
```bash
python3 ../../scripts/create_header.py header.txt
```
The above instruction creates the stub files, namely `mask.c` and `mask.h`. Continue with:
```bash
clang -c mask.c -fcf-protection=full
rm main
PROTECT_JMP=True clang -g -O0 -fcf-protection=full -fpass-plugin=../../llvm-passes/BitMasks/build/libBitmask.so -fuse-ld=lld -Wl,-z,relro,-z,now -Wl,--dynamic-linker=$ROOT_DIR/libraries/instrumented_libs/ld-linux-x86-64.so.2 -Wl,-rpath,$ROOT_DIR/libraries/instrumented_libs -Wl,--allow-shlib-undefined -rdynamic -o main main.c prog2.c mask.o
```
Notice that the compilation now includes the generated stub, `mask.o`. If you run main at this point, you will get a segmentation fault.
This happens because the callback offsets (such as those used in .init_array) are now incorrect. Adding the stub to the compiled binary *changes the symbol offsets*, which makes the previously collected values invalid.
To fix this, repeat the entire previous sequence of commands. This time, the offsets will be correct, since no additional code will be added to the final binary.\
Therefore execute:
```bash
python3 ../../scripts/callback_parser.py \
  ./libraries.json \
  dynsym.log \
  main \
  1 \
  MB \
  reachable_structs \
  sym.log \
  > output.txt

python3 ../../scripts/parse_output.py output.txt header.txt bin
cat "${ROOT_DIR}/header.txt" >> header.txt
python3 ../../scripts/create_header.py header.txt
clang -c mask.c -fcf-protection=full
rm main
PROTECT_JMP=True clang -g -O0 -fcf-protection=full -fpass-plugin=../../llvm-passes/BitMasks/build/libBitmask.so -fuse-ld=lld -Wl,-z,relro,-z,now -Wl,--dynamic-linker=$ROOT_DIR/libraries/instrumented_libs/ld-linux-x86-64.so.2 -Wl,-rpath,$ROOT_DIR/libraries/instrumented_libs -Wl,--allow-shlib-undefined -rdynamic -o main main.c prog2.c mask.o
```

Now the program is ready to run.
```bash
➜  ./main
Enter a number: 23
Instrumented with PLaTypus!
Your number is 23
Printed by normal PLT of puts (outside the masking range).
Adderss of fake puts PLT at: 0x5a7ba492f1d0
Printed by a callback!
```

Note that the extra recompilation steps required at the end only need to be applied to the stub files (`mask.c` and `mask.h`). For complex binaries that require compiling many files, the usual approach is to compile everything once. Then, after removing the generated binary, the object files remain unchanged, so only the stub file needs to be recompiled before linking everything again to produce the final main binary.
The whole process can be automated, but we document the steps here for completeness.


The previous steps are always required for every main binary, regardless of its complexity. This can also be verified by checking the provided compilation files for the six benchmarks: `redis`, `sqlite`, `nginx`, `memcached`, `bftpd`, and `lighttpd`.