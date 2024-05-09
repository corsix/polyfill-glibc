## How does polyfill-glibc work?

polyfill-glibc interprets the metadata within an ELF file to determine its dependencies (you can run `polyfill-glibc --print-imports FILE` to see these). Dependencies on a particular version of glibc come in the form of functions or variables with a `@GLIBC_2.X` suffix. polyfill-glibc has various strategies for reworking these dependencies.

### Stripping the version suffix

The simplest strategy is to remove the `@GLIBC_2.X` suffix from the function or variable name. The most common example of this is the `memcpy@GLIBC_2.14` function, which polyfill-glibc reworks to instead be a dependency on plain `memcpy`. If there is a `@GLIBC_2.2.5` variant of the function, then the plain name will bind to that; otherwise the plain name will bind to the most recent version of the function.

This strategy can only be employed when the exact version of the function doesn't matter. In the case of `memcpy`, it is undefined behaviour if the source and destination regions overlap, and the `memcpy@GLIBC_2.14` version was introduced so that glibc could become more aggressive in exploiting that undefined behaviour. Well-written programs should not be invoking undefined behaviour in the first place, so the exact version of `memcpy` shouldn't matter.

### Changing the function name

Sometimes glibc will introduce a new name for a function, or otherwise introduce a function that is behaviourally identical to an existing function. One example of this is `stdc_first_trailing_one_ui@GLIBC_2.39`, which is behaviourally identical to `ffs@GLIBC_2.2.5`, so polyfill-glibc can replace a dependency on `stdc_first_trailing_one_ui@GLIBC_2.39` with one on `ffs@GLIBC_2.2.5`.

### Changing the library

Version 2.34 of glibc merged `libdl.so`, `libpthread.so`, `librt.so`, `libanl.so`, and `libutil.so` in to `libc.so`. For example, `dlsym@GLIBC_2.34` comes from `libc.so`, but polyfill-glibc can replace it with a dependency on `dlsym@GLIBC_2.2.5`, provided that polyfill-glibc ensures `libdl.so` is loaded.

### Statically linking a standalone implementation

polyfill-glibc contains its own implementations of various glibc functions, and can statically link these implementations into other files. For example, `thrd_yield@GLIBC_2.28` can be implemented in 12 bytes of x86-64 machine code; polyfill-glibc can add these 12 bytes of executable code to a file, and then replace a `thrd_yield@GLIBC_2.28` dependency with a direct local reference to the added bytes.

### Statically linking an implementation built atop older glibc functions

Per the previous strategy, polyfill-glibc contains its own implementations of various glibc functions, and can statically link these implementations into other files. Sometimes these implementations call into older glibc functions; for example the polyfill-glibc implementation of `thrd_sleep@GLIBC_2.28` calls `clock_nanosleep@GLIBC_2.17`. A particularly common pattern is for the polyfill-glibc implementation of a function to call `__errno_location@GLIBC_2.2.5` when it needs to set `errno`.

### Adding a traditional ELF hash table

If an ELF file exports symbols, said symbols need to be listed in _some_ hash table. That hash table can either be a traditional ELF hash table, or a GNU hash table. Support for GNU hash tables was added to glibc's dynamic linker in version 2.5, so if `--target-glibc=2.4` or older is requested, then polyfill-glibc will add a traditional ELF hash table with the same contents as the GNU hash table.

Note that this can also be explicitly requested via `--add-hash`.

### Expanding or interpreting DT_RELR

Version 2.36 of glibc's dynamic loader added support for `DT_RELR`, which is a compressed format for representing certain types of relocation. polyfill-glibc contains two strategies for dealing with this: it can decompress `DT_RELR` to a format understood by older versions of glibc's dynamic loader, or it can statically link a little piece of code that understands `DT_RELR` and hook up said code to run during relocation processing by means of a dummy `IRELATIVE` relocation.

### Removing PLT-rewriting support

Version 2.39 of glibc's dynamic loader added support for PLT rewriting. This required extra metadata on `R_X86_64_JUMP_SLOT` relocations, and the `r_addend` field was appropriated for this purpose. This field was previously ignored by versions 2.36 through 2.38, but _was_ used by versions prior to 2.36, so if `--target-glibc=2.35` or older is requested, then the `r_addend` field of `R_X86_64_JUMP_SLOT` relocations needs changing to contain zero. As this removes the metadata required for PLT rewriting, PLT rewriting gets disabled at the same time (by removing the `DT_X86_64_PLTENT` tag).
