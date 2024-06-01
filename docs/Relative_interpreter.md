## Relative interpreter

A dynamically linked ELF executable normally has to specify the absolute path of the dynamic loader that it expects to use. This path will typically be something like `/lib64/ld-linux-x86-64.so.2`, though it can be set to something else at link time via `-Wl,--dynamic-linker=`, or after the fact via `polyfill-glibc --set-interpreter=`.

It is not normally possible to use the `$ORIGIN` placeholder as part of the path to the dynamic linker, however an experimental tool in the polyfill-glibc repository makes this possible. The tool can be built using the command:

```
$ ninja build/x86_64/set_relative_interp
```

> [!NOTE]
> The tool is currently only available for x86_64. If other architectures are of interest to you, open a GitHub issue so that we can gauge demand.

Once built, the tool takes two command line arguments: the path of an executable to modify, and an interpreter path starting with `$ORIGIN`, for example:

```
$ build/x86_64/set_relative_interp a.out '$ORIGIN/libs/ld-linux-x86-64.so.2'
```

> [!WARNING]
> Be careful to avoid having your shell expand `$ORIGIN`.

The modified executable will look for a dynamic loader in two places:
1. The `LD_SO_PATH` environment variable, if set.
2. The path specified as the 2<sup>nd</sup> argument to `set_relative_interp`.

If the modified executable does not have an rpath or runpath attribute, then it'll gain an rpath equal to the directory containing the dynamic loader.
