# Glibc-PlaTypus

We used a version of glibc that can be compiled with Clang. In particular, we used the `azanella/clang` branch from [this](https://github.com/zatrazz/glibc) repository. Unfortunately, at the time of writing, that branch has been removed from the remote repository, so cloning it directly is no longer possible. Therefore, we provide here the repository files, excluding the `build` directory in which we built `glibc`. The only modification is that the repository has already been annotated by our CBG annotator script, but this does not affect compilation.

To build the library the following execute the `libc_build.sh` script. This builds in 2 separate directories (`build` and `build-uninstrumented`) the instrumented and uninstrumented glibc respectively. For the instrumented version we provide the `dso_callbacks.cpp` file but it can also be produced by modifying the `clang-wrapper.sh` (so that the `LogStructs` pass is applied instead of the `BitMasks`).

We care about 2 binaries. The `libc.so` and the `ld.so`. These later need to be copied in the folder which holds all the instrumented (or unisntrumented) libraries, so that the binaries can leverage them.