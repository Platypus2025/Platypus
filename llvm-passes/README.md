# Platypus llvm passes

To prepare each pass enter the corresponding directory and define the path to the compiler via setting the `LLVM_ROOT` variable.
In case you want to use a different (clang) compiler toolchain, just change the paths inside the `CMakeLists.txt` of the corresponding pass.
After these, execute the following in each pass directory (in the example we use `FindDynSym`):

```bash
cd FindDynSym/

mkdir build
cd build
cmake ../

make -j$(nproc)
```

The corresponding pass library has been built and is ready for use.


> [!WARNING]  
> The `BitMasks` pass requires updating the path to `dso_callbacks.cpp` in the `BitMasks.cpp` file (see its included headers).  
> This also requires recompiling the pass each time `dso_callbacks.cpp` is modified.