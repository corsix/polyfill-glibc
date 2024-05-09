## polyfill-glibc command line options

The general syntax of `polyfill-glibc` is:
```
polyfill-glibc FLAG... FILENAME...
```
At least one flag and one filename must be specified. Most flags indicate some kind of action (either printing information from a file, or modifying a file). These actions are performed one at a time, from left to right. If multiple filenames are specified, then every action will be performed against every file.

The flagship action is `--target-glibc`.

Files are modified in-place, unless `--output` is used to specify a different output filename.

Actions that print information about a file: `--print-imports`, `--print-exports`, `--print-kernel-version`, `--print-interpreter`, `--print-flags`, `--print-rpath`, `--print-runpath`, `--print-soname`.

Actions that modify files: `--target-glibc`, `--remove-kernel-version`, `--rename-dynamic-symbols`, `--clear-symbol-version`, `--weak-verneed`, `--remove-verneed`,  `--set-rpath`, `--set-runpath`, `--set-soname`, `--add-early-needed`, `--add-late-needed`, `--add-hash`, `--add-gnu-hash`, `--add-debug`, `--add-flags`, `--add-rpath`, `--add-runpath`, `--set-interpreter`, `--remove-debug`, `--remove-flags`, `--remove-needed`, `--remove-relro`, `--remove-rpath`, `--remove-runpath`, `--remove-soname`.

Flags that change the effect of other actions: `--output`, `--dry`, `--page-size`, `--use-polyfill-so`, `--create-polyfill-so`, `--polyfill-cfi`.

## --target-glibc

Takes as an argument a glibc version, for example `--target-glibc=2.17`. The action identifies all dependencies that the file has on glibc functions and variables newer than the specified version, and employs [various strategies](How_does_polyfill_glibc_work.md) to remove those dependencies.

Note that `--target-glibc` is currently only implemented for x86_64 files. If other architectures are of interest to you, open a GitHub issue so that we can gauge interest.

If there are any strong dependencies that `--target-glibc` cannot remove, then it'll fail and report the problematic dependencies, for example:

```
Cannot change target version of FILENAME to 2.3.4 (x86_64) due to missing knowledge about how to handle:
  faccessat@GLIBC_2.4
  fchmodat@GLIBC_2.4
  fdopendir@GLIBC_2.4
  __isoc23_sscanf@GLIBC_2.38
  openat64@GLIBC_2.4
  __realpath_chk@GLIBC_2.4
  splice@GLIBC_2.5
  __syslog_chk@GLIBC_2.4
```

If you are hitting this, then some options are:
  * Open a GitHub issue requesting that `--target-glibc` be taught how to handle the problematic symbols.
  * Specify a different value for `--target-glibc`. For example, given the above, `--target-glibc=2.38` would succeed, and `--target-glibc=2.5` would succeed if it weren't for `__isoc23_sscanf@GLIBC_2.38`.
  * Use `--rename-dynamic-symbols` or `--clear-symbol-version` prior to `--target-glibc` to manually resolve problematic dependencies. For example, given the above, if you know that the C23 version of `sscanf` isn't required, and that the C99 version of `sscanf` would suffice, then `--rename-dynamic-symbols` could be used to rename `__isoc23_sscanf@GLIBC_2.38` to `__isoc99_sscanf@GLIBC_2.7`.

One of the strategies employed by `--target-glibc` involves statically linking new pieces of executable code in to the file. If you'd prefer to keep said pieces of executable code in a separate shared object, see `--create-polyfill-so` and `--use-polyfill-so`. The `--polyfill-cfi` flag can also be used to control whether unwind information is provided for said statically linked code.

## --output and --dry

By default, polyfill-glibc modifies files in-place. If this is not desirable, then `--output` can be specified, which takes a filename as an argument, as in `polyfill-glibc --target-glibc=2.17 --output=output_filename input_filename`. If `--output` is specified multiple times, then the last occurrence takes effect. Note that it rarely makes sense to use `--output` with multiple input filenames, as `--output` cannot be specified per input.

The `--dry` flag is shorthand for `--output=/dev/null`: polyfill-glibc will try to perform the requested actions, and will fail if it cannot perform them, but won't write out a modified file in case of success.

## --rename-dynamic-symbols

Takes as an argument the name of a file, for example `--rename-dynamic-symbols=my_renames.txt`. Said file should be a UTF-8 text file, with a pair of symbol names per line: the symbol to rename, followed by the symbol to rename to. `//` comments are supported.

As a small example, `my_renames.txt` could contain: 
```
memcpy@GLIBC_2.14 memcpy
clock_settime@GLIBC_2.17 librt.so.1::clock_settime@GLIBC_2.2.5
__isoc23_sscanf@GLIBC_2.38 __isoc99_sscanf@GLIBC_2.7
```
As a much larger example, see [the file used by `--target-glibc`](../src/x86_64/renames.txt).

The symbol to rename should either be a plain identifier, or two identifiers with an `@` sign between them. If a plain identifier is used, then it'll only match against unversioned symbols (this is different to PatchELF's `--rename-dynamic-symbols`).

The symbol to rename to should similarly be a plain identifier, or two identifiers with an `@` sign between them. All of this can be optionally prefixed with `LIBRARY_NAME::`, the presence of which has three effects:
  1. If the rename is applied, ensures that the file has a dependency on `LIBRARY_NAME` (similar to `--add-late-needed LIBRARY_NAME`).
  2. If the rename is applied, and the symbol to rename to ends with `@VERSION`, then the dependency on `LIBRARY_NAME` will additionally require that `VERSION` is present in said library.
  3. The rename will not be applied to exported symbols (i.e. it'll only be applied to imported symbols), as it makes no sense to rename an export to a different library.

As a special case, the symbol to rename to can be specified as `polyfill::NAME`, where `NAME` must be one of public functions or variables that polyfill-glibc can statically link into other files. In this case, if the rename is applied, then said function or variable will be statically linked into the file, and the symbol will be repointed to this statically linked function or variable. As a special case on top of a special case, if `--use-polyfill-so=LIB` is specified, then occurrences of `polyfill::NAME` are changed to `LIB::NAME@POLYFILL`, and then no static linking occurs.

If all symbols with a particular `@VERSION` suffix are renamed away, then the dependency on `VERSION` is automatically removed too. The dependency on the original shared library will remain though; use `--remove-needed=LIB` _after_ `--rename-dynamic-symbols` to remove any such dependencies that are no longer required.

## --print-kernel-version and --remove-kernel-version

An ELF file can specify the minimum operating system kernel version it requires to run (this is the `.note.ABI-tag` section), however this functionality is of decreasing utility: the kernel itself ignores this requirement when launching programs, and the glibc dynamic loader has also started [ignoring it for shared libraries since glibc 2.36](https://github.com/microsoft/WSL/issues/3023#issuecomment-1179796349). In other words, `.note.ABI-tag` has no effect on modern systems, and exists merely as a footgun for older systems.

The `--print-kernel-version` flag will print the required kernel version, for example:
```
Linux 3.2.0
```
Or:
```
No minimum kernel version specified.
```

The `--remove-kernel-version` flag will remove any kernel version requirement from a file, thereby giving the modern glibc behaviour even on older systems. Note that removing the requirement is just skipping a check at shared object load time; if the shared object requires a recent kernel for some functionality, it'll likely fail when it tries to exercise said functionality. The flag is similar in effect to running `strip` with `--remove-section=.note.ABI-tag`.

## rpath and runpath (--print-rpath, --print-runpath, --set-rpath, --set-runpath, --add-rpath, --add-runpath, --remove-rpath, --remove-runpath)

An ELF file can specify where to find the shared objects it depends upon. There are two different mechanisms for this, which have the very similar names _rpath_ and _runpath_:
  1. _rpath_ is a list of directories, which are searched before any directories in `LD_LIBRARY_PATH`, and are searched for both direct and transitive dependencies.
  2. _runpath_ is a list of directories, which are searched after any directories in `LD_LIBRARY_PATH`, and are searched only for direct dependencies.

Notably, both _rpath_ and _runpath_ support the `$ORIGIN` placeholder, which will be expanded by the dynamic linker to the directory of the ELF file in which `$ORIGIN` is used. This allows an ELF file's dependencies to be packaged alongside or nearby in the file system. For example, _rpath_ of `$ORIGIN` will look in the same directory for dependencies, and an _rpath_ of `$ORIGIN/lib` will look in the `lib` subdirectory. If making use of this, either set an _rpath_ on the root file in the dependency tree, or set _runpath_ on all non-leaf files in the dependency tree.

If an ELF file contains both _rpath_ and _runpath_, then _runpath_ takes priority and _rpath_ is ignored.

See [the Linux loading process](The_Linux_loading_process.md) for a full description of where _rpath_ and _runpath_ fit in.

The `--print-runpath` flag will print the _runpath_, if any. Meanwhile, the `--print-rpath` flag will print the _rpath_, if any.

The `--remove-runpath` flag will remove the _runpath_, if any. Meanwhile, the `--remove-rpath` flag will remove the _rpath_, if any.

The `--set-runpath` flag takes a colon-separated list of directories as an argument, and sets _runpath_ to that list. Meanwhile, `--set-rpath` flag is simiar, but for _rpath_. If `--set-rpath` is specified, then it also has the effect of `--remove-runpath`.

The `--add-runpath` flag takes a colon-separated list of directories as an argument, and _appends_ that list to _runpath_. Meanwhile, `--add-rpath` flag is simiar, but for _rpath_. If `--add-rpath` is specified, then it also has the effect of `--remove-runpath`.

## soname (--print-soname, --set-soname, --remove-soname)

An ELF shared library can specify a _soname_, which has two effects:
  1. `ldconfig` will create a symlink from the _soname_ to the actual file name of the shared library.
  2. Once a shared library has been loaded via its actual file name, any subsequent attempt (within the same process) to load its _soname_ will immediately resolve to said shared library rather than searching the filesystem.

The `--print-soname` flag will print the _soname_, if any.

The `--set-soname` flag takes an argument, and sets the _soname_ to that.

The `--remove-soname` flag will remove the _soname_, if any.

## ELF load flags (--print-flags, --add-flags, --remove-flags)

An ELF file can specify various flags that tweak the behaviour of the dynamic linker:

| Flag | Behaviour |
|---|---|
| `execstack` | If set, then the main thread is given an executable stack (as are all subsequent threads created by glibc functions). This is generally considered to be a major security problem. |
| `df_bind_now` or `df_1_now` | If neither flag is set, then imported functions can be imported lazily: said functions will be looked up when they are first called. If either flag is set, then lazy importing is disabled: all imported functions are looked up when the ELF file is loaded. This can make loading slower, and also means that problems get detected early. Note that setting the `LD_BIND_NOW` environment variable also disables lazy importing. Lazy importing requires that the GOT exist in writable memory rather than read-only memory; if removing `df_bind_now` and `df_1_now`, then `--remove-relro` might also be required to keep the GOT in writable memory. |
| `df_1_pie` | If set, marks the ELF file as an executable rather than a shared library, and means that the ELF file cannot - by any means - be loaded as library. |
| `df_1_noopen` | If set, then the ELF file cannot be loaded by an explicit call to `dlopen` (but it can still be loaded by any other means; most notably by being listed as a dependency of some other ELF file, or by being named in `LD_PRELOAD`). |
| `df_1_nodeflib` | If set, then the system default search path (e.g. `/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu:/lib:/usr/lib`) is _not_ searched when trying to locate dependencies of the ELF file. Accordingly, if it has any non-absolute dependencies, _rpath_ or _runpath_ or `LD_LIBRARY_PATH` needs to be used to specify the directory in which to find them. |
| `df_1_nodelete` | If set, implicit and explicit `dlclose` calls against this file have no effect: once loaded, the ELF file remains loaded until the process exits. Note that this has no effect on musl, as `dlclose` is already a no-op there. |
| `df_1_initfirst` | If any ELF shared library specifies this flag, then the _last_ such shared library to be loaded has its init functions called before those of other shared libraries. See [the Linux loading process](The_Linux_loading_process.md) for a full description. Note this flag was reserved for `libpthread.so.0` in glibc versions prior to 2.34. |
| `df_symbolic` | If set in an ELF file that both imports and exports symbols, then the import search order is tweaked slightly: the file's own exports will be used to satisfy its imports, and only if that fails will the default search order be employed. |
| `df_textrel` | If set, all virtual memory mapped in for the ELF file is made writable while relocations are being applied, thereby allowing relocations to modify "read-only" memory. |

Many other flags also exist, but they have no effect.

The `--print-flags` flag to polyfill-glibc will print all the set flags. 

The `--add-flags` flag to polyfill-glibc takes as an argument a list of flags (separated by spaces, commas, pipes, or colons), and sets all of them. For example, `--add-flags=df_bind_now,df_1_nodelete` sets the `df_bind_now` and `df_1_nodelete` flags.

The `--remove-flags` flag to polyfill-glibc takes as an argument a list of flags (separated by spaces, commas, pipes, or colons), and unsets all of them. For example, `--remove-flags=execstack,df_noopen` unsets the `execstack` and `df_noopen` flags.

## ELF hash tables (--add-hash and --add-gnu-hash)

If an ELF file exports symbols, said symbols need to be listed in _some_ hash table. That hash table can either be a traditional ELF hash table, or a GNU hash table. For maximum compatibility (though at the cost of increased file size), a file can have both styles of hash table.

The `--add-hash` flag causes a traditional ELF hash table to be added, if not already present. This hash table gives maximum compatibility, in particular giving compatibility with glibc dynamic loader versions prior to 2.5 (which was released in September 2006).

The `--add-gnu-hash` flag causes a GNU hash table to be added, if not already present. This hash table has better performance characteristics than the traditional ELF hash table.

## --polyfill-cfi

In some cases, `--target-glibc` and `--rename-dynamic-symbols` can cause additional executable code to be statically linked into the file. Said executable code can have so-called CFI associated with it, which serves two purposes:
  1. It allows exceptions to be thrown over it, for example throwing an exception in the `qsort_r` or `twalk_r` callback, and catching it in the `qsort_r` or `twalk_r` caller.
  2. It allows asynchronous signal handlers (such as profilers) to generate a stack trace when the signal fires during execution of the statically linked code. Note that all of polyfill-glibc's statically linked code also maintains a frame pointer, which profilers might prefer to use for generating a stack trace.

There are two downsides to CFI:
  1. It occupies space (in the file on disk, and in virtual memory when the file is loaded).
  2. Registering the CFI with the C/C++ runtime often requires calling `__register_frame_info` / `__deregister_frame_info` from `libgcc_s.so.1`.

The `--polyfill-cfi` flag controls whether CFI is provided for statically linked functions. The available settings are:

| Setting | Behaviour |
|---|---|
| `--polyfill-cfi=full` | Provide CFI for all statically linked functions. |
| `--polyfill-cfi=auto` (default) | If any statically linked function has non-trivial CFI, or if CFI can be provided without needing to call `__register_frame_info` / `__deregister_frame_info`, then provide CFI for all statically linked functions. |
| `--polyfill-cfi=minimal` | Provide CFI for statically linked functions with non-trivial CFI information (do not provide CFI for any statically linked functions whose CFI would be trivial). |
| `--polyfill-cfi=none` | Do not provide CFI for any statically linked functions. |

## --create-polyfill-so and --use-polyfill-so

In some cases, `--target-glibc` and `--rename-dynamic-symbols` can cause additional executable code to be statically linked into the file. For some people, this can be undesirable, and so `--create-polyfill-so` and `--use-polyfill-so` are provided instead, with the effect of putting the additional executable code in a separate shared object and then dynamically linking against that shared object.

If the `--create-polyfill-so` flag is specified, then the behaviour of `--rename-dynamic-symbols` is modified: whenever `polyfill::NAME` is given as the new symbol name (regardless of whether the old symbol name is present), then `NAME@POLYFILL` is added as an _export_ to the file, with the statically linked code as the implementation of that export. The behaviour of `--target-glibc` is similarly modified: any statically linked code that _could_ be used to remove a problematic dependency (regardless of whether the problem is present) is added an _export_ to the file. The `--create-polyfill-so` flag is usually used in combination with the special input filename `empty:x86_64`, which denotes an empty ELF file. Accordingly, an invocation like the following can be used to create a shared object called `polyfills.so` with all the code required to polyfill back to glibc 2.3.2:
```
polyfill-glibc empty:x86_64 --add-hash --add-gnu-hash --create-polyfill-so --target-glibc=2.3.2 --output polyfills.so
```

Once such a shared object has been created, it can be used with `--use-polyfill-so`, which takes as argument the name of a shared library, for example `--use-polyfill-so=polyfills.so`. If `--use-polyfill-so=LIB` is specified, then the behaviour of `--rename-dynamic-symbols` is modified: whenever `polyfill::NAME` is given as the new symbol name, this is changed to `LIB::NAME@POLYFILL`. The behaviour of `--target-glibc` is similarly modified: any code used to remove a problematic dependency is dynamically imported from `LIB` rather than statically linked into the file.

The `LIB` in `--use-polyfill-so=LIB` should usually be a file name with no `/` characters in it. If neccessary, use an _rpath_ or _runpath_ to specify the directory in which `LIB` will be found at runtime.

## --add-early-needed, --add-late-needed, --remove-needed

These take the name of a shared library as an argument, and either add or remove a dependency on that shared library.

The `--add-early-needed` flag is similar to `LD_PRELOAD`; the named shared library will be searched for symbols _before_ any other libraries that the ELF file depends upon. The `--add-late-needed` flag is the opposite; the named shared library will be searched for symbols after any other libraries that the ELF file depends upon. Note that neither flag has any effect if the ELF file already depends on the named library.

The `--remove-needed` flag has no effect if the ELF file doesn't depend on the named library.

## ELF interpreter (--print-interpreter, --set-interpreter)

A dynamically linked ELF file specifies the absolute path of the dynamic loader that it expects to use. The `--print-interpreter` flag will print this path. The `--set-interpreter` flag takes a path as an argument.

## --page-size

ELF files do not specify the page size of the kernel that they expect to run on, but nevertheless it is crucial to know said page size when modifying an ELF file. If that page size is not 4096 bytes, then the `--page-size` flag should be used, which takes as an argument a decimal power of two, for example `--page-size=8192`.

## --print-imports

1. If the ELF file specifies an interpreter, then it is printed (like `--print-interpreter`).
2. If the ELF file specifies an _rpath_ or _runpath_, then that is printed (like `--print-rpath` / `--print-runpath`).
3. If the ELF file depends upon any dynamic libraries, then their names are printed.
4. If the ELF file depends upon particular versions from dynamic libraries, then these are printed.
5. Finally, the names of all imported functions and variables are printed.

## --print-exports

1. If the ELF file specifies a soname, then it is printed (like `--print-soname`).
2. If the ELF file depends upon any dynamic libraries, then their names are printed. This might seem weird, but any exports from such libraries get transitively re-exported.
3. If the ELF file provides particular versions, then their names are printed.
4. Finally, the names of all exported functions and variables are printed.

## --clear-symbol-version

Takes as an argument a comma separated list of symbols, for example `--clear-symbol-version=memcpy,exp2`. If the ELF file imports or exports any of these symbols _with_ a version suffix, then said version suffix is removed. For example, imports or exports of `memcpy@GLIBC_2.14` or `memcpy@GLIBC_2.2.5` would be replaced with plain `memcpy`. For imported symbols, the resultant ELF file will bind to _some_ version of `memcpy`.

If all symbols with a particular `@VERSION` suffix are renamed away, then the dependency on `VERSION` is automatically removed too.

While it looks like a very useful tool, be aware that it is _not_ a panacea, and is often dangerous to use. For example, clearing the symbol version from `__libc_start_main@GLIBC_2.34` might look tempting, but the resultant file will not work properly on glibc versions prior to 2.34 (use `--target-glibc` to remove a dependency on this function, or use `--rename-dynamic-symbols` to rename it to `polyfill::__libc_start_main_2_34`).

## --remove-verneed

This flag is equivalent to `--clear-symbol-version` applied to every single symbol name imported or exported by the ELF file.

This is an extremely dangerous tool.

## --weak-verneed

This flag causes all imported versions to be weakly imported rather than the default of strongly imported. The effect of this is that errors during application startup along the lines of ``/lib/x86_64-linux-gnu/libc.so.6: version `GLIBC_2.28' not found (required by ./my-program)`` are no longer fatal: application startup will proceed in spite of the errors.

While it looks like a very useful tool, be aware that it is _not_ a panacea, and the application will likely fail when it first tries to call a function from a missing version.

## --add-debug and --remove-debug

ELF executables can optionally have a `DT_DEBUG` record in their ELF dynamic array. See [rtld-debugger-interface.txt](https://github.com/bminor/glibc/blob/eb59c7b43dd5c64c38e4c3cd21e7ad75d8d29cb0/elf/rtld-debugger-interface.txt) for details.

The `--add-debug` flag will add a `DT_DEBUG` record if one isn't already present.

The `--remove-debug` flag will remove any `DT_DEBUG` record, if present.

## --remove-relro

An ELF file can specify that a particular range of virtual memory should be readable and writable whilst relocations are being applied, but then made read-only once all relocations have been applied. This is generally considered a useful security feature, but disabling it can sometimes be neccessary.

The `--remove-relro` flag will remove the `PT_GNU_RELRO` program header, thereby causing the relevant range of virtual memory to remain writable once all relocations have been applied.

See also the `df_textrel` flag, the presence of which makes _all_ of the ELF file's virtual memory writable while relocations are being applied.
