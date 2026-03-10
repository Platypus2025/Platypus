# Platypus llvm passes

To prepare each pass enter the corresponding directory and do the following:

```bash
cd FindDynSym/

mkdir build
cd build
cmake ../

make -j$(nproc)
```

The corresponding pass library has been built and is ready for use.
Remember to change the compiler path used in the `CMakeLists.txt` in case you want to compile the pass with your own compiler.