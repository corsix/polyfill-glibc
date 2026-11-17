## polyfill-glibc

How often have you compiled a C/C++ program on a recent Linux system, tried to run that compiled program on an older Linux system, and then hit a GLIBC version error?
Concretely, perhaps you're seeing something like this:

```
new-system$ gcc my-program.c -o my-program
old-system$ scp new-system:my-program .
old-system$ ./my-program
./my-program: /lib/x86_64-linux-gnu/libc.so.6: version `GLIBC_2.28' not found (required by ./my-program)
```

The motivating idea behind polyfill-glibc is that an extra post-compilation step can prevent these errors from happening:

```
new-system$ gcc my-program.c -o my-program
new-system$ polyfill-glibc --target-glibc=2.17 my-program
old-system$ scp new-system:my-program .
old-system$ ./my-program
It works!
```

## Build and run instructions

To build polyfill-glibc, you'll need a git client, a C11 compiler (such as `gcc`), and [`ninja`](https://github.com/ninja-build/ninja/wiki/Pre-built-Ninja-packages). With these tools present, the build process is:

```
$ git clone https://github.com/corsix/polyfill-glibc.git
$ cd polyfill-glibc
$ ninja polyfill-glibc
```

Once built, pass the oldest glibc version you want the file to target and the path to the program or shared library:
```
$ ./polyfill-glibc --target-glibc=2.17 /path/to/my-program
```

By default, the input file is modified in place. Use `--output` to write the result to a separate file. polyfill-glibc changes only the specified ELF file; it does not modify the system's installed glibc.

### Check the system glibc version

Run one of these commands on the destination system:

```
$ getconf GNU_LIBC_VERSION
glibc 2.39
```

Alternatively, run `ldd --version` and read the glibc version from its first line. Check the destination system rather than just the build system, since they may use different glibc versions.

Lowering the target version is only possible when the file's newer glibc dependencies can be handled by known renames or polyfills. If an unsupported strong dependency is found, the tool reports it and stops rather than guaranteeing a working result.

## Supported platforms and glibc versions

The `--target-glibc` operation is implemented for **x86_64** and **aarch64**. Compatibility rules for symbols introduced in glibc **2.40–2.44 are currently partial and x86_64-only**. The highest partially covered release is glibc 2.44; this does not mean every program built against glibc 2.44 can be downported. The 2.40–2.43 rules have been tested by downporting a glibc 2.43-linked executable to glibc 2.39. The glibc 2.44 mappings have not yet been runtime-tested.

See [Limitations](docs/Limitations.md) for the covered and unsupported symbol families.

## Common arguments

| Argument | What it does |
|---|---|
| `--target-glibc=VERSION` | Tries to remove dependencies on glibc newer than `VERSION`, using known symbol renames and polyfills. |
| `--output=FILE` | Writes the modified file to `FILE` instead of changing the input in place. |
| `--dry` | Checks whether the requested changes can be applied without writing an output file. |
| `--print-imports`, `--print-exports` | Lists the ELF file's imported or exported symbols. |
| `--set-rpath=PATH`, `--set-runpath=PATH`, `--set-soname=NAME` | Sets common ELF dynamic-linker metadata. |
| `-h`, `--help` | Prints usage, examples, available options, and the highest partially covered glibc release. |

For the complete option list and details, see [Command line options](docs/Command_line_options.md).

## License

polyfill-glibc itself is made available under the [MIT license](https://opensource.org/license/mit). The polyfilling procedure can sometimes involve linking small pieces of polyfill-glibc into the file being modified; the "the above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software" clause of the MIT license is explicitly waived for any such pieces.
