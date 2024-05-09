## Comparing polyfill-glibc with PatchELF

NixOS's [PatchELF](https://github.com/NixOS/patchelf) tool is similar to polyfill-glibc, in that both are designed for modifying ELF executables and shared libraries after they have been built.

The flagship feature of polyfill-glibc is the `--target-glibc` operation, which has no direct equivalent in PatchELF (though a limited subset of the functionality can be achieved via PatchELF's `--rename-dynamic-symbols` and `--clear-symbol-version`). A lot of lower level operations have equivalents between polyfill-glibc and PatchELF:

| polyfill-glibc operation | PatchELF operation |
| ----------- | -------- |
| `--add-debug` | `--add-debug-tag` |
| `--add-flags execstack` | `--set-execstack` |
| `--add-flags nodeflib` | `--no-default-lib` |
| `--add-hash` / `--add-gnu-hash` | N/A |
| `--add-early-needed` / `--add-late-needed` | `--add-needed` |
| `--add-rpath` / `--add-runpath` | `--add-rpath` |
| `--clear-symbol-version` | `--clear-symbol-version` |
| `--print-exports` | N/A |
| `--print-flags` then `grep execstack` | `--print-execstack` |
| `--print-imports` then `grep '^library '` | `--print-needed` |
| `--print-interpreter` | `--print-interpreter` |
| `--print-kernel-version` | N/A |
| `--print-os-abi` | `--print-os-abi` |
| `--print-runpath` / `--print-rpath` | `--print-rpath` |
| `--print-soname` | `--print-soname` |
| `--rename-dynamic-symbols` | `--rename-dynamic-symbols` |
| `--remove-debug` | N/A |
| `--remove-flags execstack` | `--clear-execstack` |
| `--remove-kernel-version` | N/A |
| `--remove-needed` | `--remove-needed` |
| `--remove-relro` | N/A |
| `--remove-runpath` / `--remove-rpath` | `--remove-rpath` |
| `--remove-soname` | N/A |
| `--remove-verneed` | N/A |
| N/A | `--replace-needed` |
| `--set-interpreter` | `--set-interpreter` |
| N/A | `--set-os-abi` |
| `--set-runpath` / `--set-rpath` | `--set-rpath` |
| `--set-soname` | `--set-soname` |
| N/A | `--shrink-rpath` |
| `--target-glibc` | N/A |
| `--weak-verneed` | N/A |

Behind the scenes, one notable difference between polyfill-glibc and PatchELF is that PatchELF requires the file being modified to have ELF section headers, whereas polyfill-glibc does not. In turn, this influences how the two tools work around [the kernel's `AT_PHDR` bug](Kernel_AT_PHDR_bug.md): PatchELF can leave the program headers in their original location and move sections as neccessary to make space for the enlarged program headers, whereas polyfill-glibc moves the program headers to a new location and inserts a zero-length `PT_LOAD` as a workaround.
