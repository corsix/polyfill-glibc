## Stage 1: execve in the kernel

When `execve` is called, control flow eventually reaches the [`load_elf_binary` function in `binfmt_elf.c`](https://github.com/torvalds/linux/blob/8d025e2092e29bfd13e56c78e22af25fac83c8ec/fs/binfmt_elf.c#L819), which does the following:
1. Read the ELF header of the target executable into temporary memory, and check that it looks sane.
2. Read the program headers of the target executable into temporary memory.
3. Allocate a range of virtual memory for the initial thread's stack, honouring `p_flags & PF_X` of the `PT_GNU_STACK` program header (if present).
4. Allocate a range of virtual memory for the target executable's code and data, and call `mmap` once for each `PT_LOAD` program header to populate that virtual memory.
5. If there is a `PT_INTERP` header:
    1. Open the file named by `PT_INTERP` (which must be an absolute path, or relative to the current working directory, with no support for placeholders). This is often something like `/lib64/ld-linux-x86-64.so.2`, i.e. [glibc's dynamic loader](What_is_glibc.md).
    2. Read its ELF header into temporary memory, and check that it looks sane.
    3. Read its program headers into temporary memory.
    4. Allocate a range of virtual memory for its code and data, and call `mmap` once for each `PT_LOAD` program header to populate that virtual memory.
6. Allocate a range of virtual memory for the vDSO's code and data, and populate it.
7. Construct the auxiliary information vector, keep a copy of it in `/proc/self/auxv`, and also push it on to the stack. Amongst other things, this contains:
   
   | Key | Contents |
   | --- | -------- |
   | `AT_PHDR` | The virtual address of the target executable's program headers (as mapped in during step 4, not the temporary copy from step 2). This is [potentially buggy in older kernels](Kernel_AT_PHDR_bug.md). |
   | `AT_PHENT` | `e_phentsize` from the target executable's ELF header. |
   | `AT_PHNUM` | `e_phnum` from the target executable's ELF header. |
   | `AT_ENTRY` | `e_entry` from the target executable's ELF header, adjusted by the load offset from step 4. |
   | `AT_BASE` | The load offset of the interpreter from step 5iv (0 if there was no `PT_INTERP` in step 5). |
   | `AT_SYSINFO_EHDR` | The virtual address of the ELF header of the vDSO (from step 6). |
   | `AT_RANDOM` | The virtual address of 16 bytes of random data (for seeding libc's PRNG). |
   | `AT_EXECFN` | The virtual address of the filename passed to `execve` (usually similar to `argv[0]`). |
   | `AT_SECURE` | A boolean indicating whether the target executable was setuid / setgid / setcap. |

8. Push `argc`, `argv[]`, and `envp[]` on to the stack.
9. Free the temporary memory from steps 2, 3, 5ii, and 5iii.
10. Transfer control to userspace. If there was a `PT_INTERP` in step 5, control will go to `e_entry` from the interpreter's ELF header, adjusted by the load offset from step 5iv. Otherwise, control will go to `e_entry` from the target executable's ELF header, adjusted by the load offset from step 4.

At this point, the kernel considers its work to be done. Note that it completely ignores `PT_PHDR`, `PT_DYNAMIC`, `PT_GNU_RELRO`, `PT_GNU_EH_FRAME`, `PT_TLS`, along with any other program headers. ELF section headers (if present) are also ignored. If there was no `PT_INTERP` in step 5, then the target executable's `e_entry` takes over from here.

## Stage 2: libc's dynamic loader

If there was a `PT_INTERP` in step 5, then the interpreter's `e_entry` takes over from here. If the interpreter is glibc's dynamic linker, then some of what it does is:
1. Load any shared libraries named in the `LD_PRELOAD` environment variable (` ` or `:` separated list of names. If `AT_SECURE`, any names with `/` in are ignored).
2. Load any shared libraries named in the `/etc/ld.so.preload` file.
3. Load any shared libraries named in the target executable's `DT_NEEDED` / `DT_AUXILIARY` / `DT_FILTER` entries. Note that `AT_PHDR` is crucial in allowing the dynamic loader to find these entries in the target executable.
4. For every shared library loaded so far, also load any shared libraries named in its `DT_NEEDED` / `DT_AUXILIARY` / `DT_FILTER` entries. Skip any entries referring to libraries already loaded. Repeat until there is nothing more to load.
5. For every shared library loaded so far, plus the target executable, if it has `DT_VERNEED`, then for every entry in `DT_VERNEED`, check that the shared library named therein has been loaded and has a matching entry in its `DT_VERDEF`. This is where errors like ``version `GLIBC_2.28' not found`` come from.
6. For every shared library loaded so far, if it has non-empty `PT_TLS`, assign it a TLS module index, and assign it some space in the static TLS region.
7. For every shared library loaded so far, in reverse dependency order, apply the relocations from its `DT_RELR` / `DT_REL` / `DT_RELA` / `DT_JMPREL` entries. This can involve doing symbol lookups against the list of loaded libraries.
8. Apply the relocations from the target executable's `DT_RELR` / `DT_REL` / `DT_RELA` / `DT_JMPREL` entries. Note that `AT_PHDR` is crucial in allowing the dynamic loader to find these entries in the target executable.
9. Initialise the values of all per-thread variables for the main thread based on the contents of `PT_TLS` regions, and take a copy of this to be used for initialising any subsequently launched threads.
10. If `libc.so.6` has been loaded, call the `__libc_early_init@GLIBC_PRIVATE` function therein.
11. If any shared library specified `DF_1_INITFIRST`, then the _last_ such shared library to be loaded has its `DT_INIT` / `DT_INIT_ARRAY` functions called. Note this flag was reserved for `libpthread.so.0` in glibc versions prior to 2.34.
12. Call functions listed in the target executable's `DT_PREINIT_ARRAY`.
13. For every shared library loaded so far, in reverse dependency order, call its `DT_INIT` / `DT_INIT_ARRAY` functions.
14. Transfer control to the target executable's `e_entry` (adjusted by its load offset).

### Searching for shared libraries

When loading shared libraries in steps 1-4 above, library names need to be converted into file paths. If the library name starts with or otherwise contains the `/` character, then any `$ORIGIN` / `$PLATFORM` / `$LIB` placeholders are expanded, and the result is treated either as an absolute path (if it starts with `/`) or as relative to the current working directory (if it contains `/` anywhere else). Otherwise, if the library name does not contain the `/` character, then a search procedure is initiated:
1. If any shared library loaded so far has `DT_SONAME` equal to the name being looked up, the name resolves to said library.
2. If the immediately referencing shared library or executable does _not_ have `DT_RUNPATH`:
    1. Search the `DT_RPATH` of the immediately referencing shared library or executable (`:` separated list of paths, `$ORIGIN` / `$PLATFORM` / `$LIB` placeholders supported).
    2. Search the `DT_RPATH` of _that_ shared library's immediately referencing shared library or executable. Repeat until the executable is reached (search that too).
3. Search the `LD_LIBRARY_PATH` environment variable (`:` or `;` separated list of paths, `$ORIGIN` / `$PLATFORM` / `$LIB` placeholders supported). This is skipped if `AT_SECURE`.
4. If the immediately referencing shared library or executable has `DT_RUNPATH`, search that `DT_RUNPATH` (`:` separated list of paths, `$ORIGIN` / `$PLATFORM` / `$LIB` placeholders supported).
5. Do a lookup against `/etc/ld.so.cache` (a file maintained by the `ldconfig` program).
6. If the immediately referencing shared library or executable has `DF_1_NODEFLIB`, fail rather than proceeding.
7. Search the system default path. On an x86_64 system, this might for example be `/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu:/lib:/usr/lib`.

### Placeholders

Three placeholders are supported in shared library names and in shared library search paths: `$ORIGIN`, `$PLATFORM`, and `$LIB`. The most interesting of these is `$ORIGIN`, which expands to the absolute path of the directory of the file in which `$ORIGIN` appears. For example, if `$ORIGIN` appears in `/home/corsix/mylib.so`, then `$ORIGIN` expands to `/home/corsix`. If `mylib.so` was subsequently moved to `/home/corsix/libs/mylib.so`, then `$ORIGIN` would expand to `/home/corsix/libs`.

The `$PLATFORM` placeholder is _meant_ to expand to a string naming the current processor architecture, for example `x86_64` on x86-64 systems. However, at some point glibc decided to change this, meaning that `$PLATFORM` now expands to `haswell` on recent x86-64 systems.

The `$LIB` placeholder expands to a string generally similar to part of the system default search path for shared libraries. On an x86_64 system, `$LIB` might for example expand to `lib/x86_64-linux-gnu`.

Note that placeholders are _not_ supported in ELF interpreter paths.

To see what `$PLATFORM` and `$LIB` expand to on your system, one option is something like:
```
$ LD_PRELOAD='/$LIB/xyz/$PLATFORM/xyz' strace true 2>&1 | grep xyz.*RDONLY
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/xyz/haswell/xyz", O_RDONLY|O_CLOEXEC) = -1 ENOENT (No such file or directory)
```

## Stage 3: entry point of target executable

The target executable's `e_entry` takes over at this point. In practice, for dynamically linked executables, the entry point aligns the stack and then jumps to glibc's `__libc_start_main` function, which does a few things:

1. Call functions listed in the target executable's `DT_INIT` / `DT_INIT_ARRAY`.
2. Call the target executable's `main` function (i.e. the `int main(int argc, const char** argv)` that C programmers are familiar with).
3. Call `exit`, passing along the return value from `main`.

The dynamic loader's job isn't over when `main` is called though, as the program could call back into it via functions such as `dlopen` or `dlsym`.
