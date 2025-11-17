### Files modified from the llvm toolchain

This folder contains the files we modified. We keep the path names the same as in the official llvm repo.

### Build
To build the modified compiler and linker you have to use the provided `compile.sh` script.

The script is going to clone the llvm repo, checkout to `20.1.0` version, change only the modified files
and then build everything inside a `build` directory in the downloaded llvm repo.
For this to work you have to provide two arguments. The first is the path to the current repo (e.g. `/path/to/Platypus`). The second is the path inside which you want to clone the llvm repo and build the toolchain. An example follows:

```
➜ ./compiler.sh /path/to/Platypus /path/to/modified/LLVM/repo
```