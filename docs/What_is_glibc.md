## What is glibc?

As far as the polyfill-glibc project is concerned, glibc is a collection of shared libraries, notably including:
* `ld-linux-${PLATFORM}.so.2`, the dynamic loader.
* `libdl.so.2`, public API of the dynamic loader. (‡)
* `libc.so.6`, large parts of the C standard library and many POSIX functions.
* `libm.so.6`, mathematical functions.
* `libmvec.so.1`, SIMD mathematical functions (since glibc 2.22 for x86_64, since glibc 2.38 for aarch64).
* `libpthread.so.0`, threading functions. (‡)
* `librt.so.1`, POSIX.1b Realtime Extension functions. (‡)
* `libresolv.so.2`, DNS functions.
* `libanl.so.1`, asynchronous DNS functions. (‡)
* `libcrypt.so.1`, cryptographic functions (removed in glibc 2.39, use [libxcrypt](https://github.com/besser82/libxcrypt/) instead).
* `libnsl.so.1`, network services functions.
* `libnss_${BACKEND}.so.2`, Name Service Switch functions.
* `libutil.so.1`, a few miscellaneous functions. (‡)

(‡) Merged into `libc.so.6` since glibc 2.34. The shared libraries still exist, but do nothing other than ensure `libc.so.6` is loaded.

Most Linux systems come with a version of glibc installed by default, though you might also find [musl](https://musl.libc.org/) or [µClibc](https://uclibc-ng.org/) filling the same role.

The authors of glibc are [ideologically opposed to statically linking against glibc](https://akkadia.org/drepper/no_static_linking.html), and certain features (e.g. locales and Name Service Switch) of glibc are designed around plugins, which require dynamic linking.

A new version of glibc is released every six months or so. The versions are compatible in one direction (programs compiled against old versions will run with newer versions), but often incompatible in the other direction (programs compiled against new versions will not necessarily run with older versions). This presents a problem for distributing Linux executables or libraries in compiled form. Various solutions to this problem have appeared over the years:
1. Use `.symver` directives to link against particular (old) versions of each glibc function, for example [wheybags/glibc_version_header](https://github.com/wheybags/glibc_version_header).
2. Use [Zig CC](https://andrewkelley.me/post/zig-cc-powerful-drop-in-replacement-gcc-clang.html) as your C compiler, with flags like `-target x86_64-linux-gnu.2.28`. For example see [uber/hermetic_cc_toolchain](https://github.com/uber/hermetic_cc_toolchain).
3. Create a sysroot containing an old version of glibc, and compile against it.
4. Create a container with an old version of glibc inside it, and compile inside the container.
5. Distribute applications as containers, with the requisite new version of glibc inside the container.

polyfill-glibc presents a new 6<sup>th</sup> solution to the problem: compile against a new glibc, then run polyfill-glibc as a post-compilation step to give compatibility with an older glibc version. It could be seen as a stronger version of [PatchELF](https://github.com/NixOS/patchelf)'s `--rename-dynamic-symbols` and `--clear-symbol-version` flags.
