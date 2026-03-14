# Evaluation

This folder contains the files necessary for the performance evaluation of the benchmarks used.

For this, it is assumed that the binaries and their libraries have already been built. Note that this must be done for both the instrumented and the uninstrumented versions of the binaries.

### Binary Sizes

To check the sizes of both the instrumented and uninstrumented DSOs and main binaries, you can use the command `ls -lh --block-size=K <binary_name>`. Note that the sizes reported in the paper (Tables 5 and 6) correspond to **stripped binaries**. Therefore, you should first strip each binary using the command `llvm-strip <binary_name>`. Ideally, use the `llvm-strip` binary built as part of our modified toolchain.