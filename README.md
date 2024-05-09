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

Once built, running it is intended to be as simple as passing the oldest version of glibc you want to support, along with the path to the program (or shared library) to modify, as in:
```
$ ./polyfill-glibc --target-glibc=2.17 /path/to/my-program
```

If running it _isn't_ this simple, then open a GitHub issue describing why not, and we'll try to improve things.

Note that at present, the `--target-glibc` operation of polyfill-glibc is only implemented for x86_64. If other architectures are of interest to you, open a GitHub issue so that we can gauge demand.

If distributing shared libraries, polyfill-glibc can also be used to inspect dependencies (with the `--print-imports` and `--print-exports` operations), and to modify how shared libraries are loaded (with the `--set-rpath`, `--set-runpath`, and `--set-soname` operations). Consult [the documentation](docs/Command_line_options.md) for a full list of command line options.

## License

polyfill-glibc itself is made available under the [MIT license](https://opensource.org/license/mit). The polyfilling procedure can sometimes involve linking small pieces of polyfill-glibc into the file being modified; the "the above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software" clause of the MIT license is explicitly waived for any such pieces.
