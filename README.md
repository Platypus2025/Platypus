# Platypus
Artifact Repository for the paper *PLATYPUS: Restricting Cross-Module Transitions to Mitigate Code-Reuse Attacks* submitted in S&P 2026 cycle 2

### Overview
* *llvm-platypus* folder contains only the files that modified from the official LLVM (20.1.0) toolchain due to size limitations of the Anonymous GitHub
* *glibc-platypus* folder contains the glibc used in the paper during evaluation
* *llvm-passes* folder contains the code of the three passes being sued in paper. Specifically *FindDynsym* and *LogStructs* are used during the symbol gatherin process (see section 3.4.2 in the paper). *BitMasks* is the pass instrumenting the code with our masking mechanism.
* In *scripts* folder there are various python scripts used mainly for creating the callback tables and the special functions linked with the main binary (see 3.4.3).

All the above require our modified LLVM compiler and linker to have been build, since they leverage them.