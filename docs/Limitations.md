## Limitations of polyfill-glibc

Currently, the `--target-glibc` operation of polyfill-glibc is only implemented for x86_64. If other architectures are of interest to you, open a GitHub issue so that we can gauge interest. Note that lower level operations, for example `--set-rpath`, are implemented for all architectures.

Polyfills have not yet been written for every glibc function. If you're hitting such a case, open a GitHub issue so that we can gauge interest.

Some glibc functions are system call wrappers. Even if the function itself is polyfilled, the underlying system call might not be present on older kernels. In such cases the wrapper function will return `-1` and set `errno` to `ENOSYS`. User code needs to detect and handle this scenario.

Only glibc functions are polyfilled by polyfill-glibc; functions from other libraries are not. In particular, libstdc++ uses symbol versions starting with `GLIBCXX_3.`, but these are libstdc++ functions rather than glibc functions.

Sometimes even a small amount of polyfilling can result in a noticable increase in binary size. This is because resizing the string table (or the symbol table, or the relocation table, or the export hash table) of an existing binary from N entries to N+1 entries requires copying the entire table to a new location and allocating N+1 entries at that new location, rather than just adding 1 entry at the old location.

As a concession to ease to implementation, polyfill-glibc needs to be able to map the entire file being edited into memory. This is rarely a problem on 64-bit systems, but can prevent editing files larger than 1-2GB on 32-bit systems.

## Limitations of particular polyfilled functions

|Function|Limitations|
|--------|-----------|
|`accept4`|See [asynchronous cancellation](Asynchronous_cancellation.md).|
|`arc4random`, `arc4random_buf`, `arc4random_uniform`|Some glibc versions of these functions contain buggy handling of `EINTR` and/or `ENOSYS` results from the `getrandom` system call (see for example [BZ#29624](https://sourceware.org/bugzilla/show_bug.cgi?id=29624) and [BZ#31612](https://sourceware.org/bugzilla/show_bug.cgi?id=31612)). The polyfill implementation does not have these bugs.|
|`copy_file_range`|See [asynchronous cancellation](Asynchronous_cancellation.md).|
|`__cxa_thread_atexit_impl`|If this function is polyfilled, then the `df_1_nodelete` flag will be applied to the file, thereby causing the file to remain in memory once loaded, even if `dlclose` is called against it. If you are confident that any destructor logic for `thread_local` variables residing in the file will not need to be called after `dlclose`, then you can specify `--remove-flags df_1_nodelete` after `--target-glibc`.|
|`epoll_pwait2`|See [asynchronous cancellation](Asynchronous_cancellation.md).|
|`fcntl64`|See [asynchronous cancellation](Asynchronous_cancellation.md).|
|`getrandom`|See [asynchronous cancellation](Asynchronous_cancellation.md).|
|`open_by_handle_at`|See [asynchronous cancellation](Asynchronous_cancellation.md).|
|`pidfd_spawn`, `pidfd_spawnp`|Some glibc versions of these functions contain a bug that leaks an fd in some failure scenarios (see [BZ#31695](https://sourceware.org/bugzilla/show_bug.cgi?id=31695)). The polyfill implementation does not have this bug.|
|`posix_spawn_file_actions_addchdir_np`|If the polyfill implementation is used, then `posix_spawn_file_actions_destroy` will also be replaced with a polyfill implementation, even if the `--target-glibc` version would not otherwise require this.|
|`posix_spawn_file_actions_addchdir_np`, `posix_spawn_file_actions_addclosefrom_np`, `posix_spawn_file_actions_addfchdir_np`, `posix_spawn_file_actions_addtcsetpgrp_np`, `posix_spawnattr_setcgroup_np`|If the polyfill implementation is used, then `posix_spawn` and `posix_spawnp` will also be replaced with a polyfill implementation, even if the `--target-glibc` version would not otherwise require this.|
|`preadv2`, `preadv64v2`|See [asynchronous cancellation](Asynchronous_cancellation.md).|
|`pwritev2`, `pwritev64v2`|See [asynchronous cancellation](Asynchronous_cancellation.md).|
|`quick_exit`|If the ambient glibc is version 2.18 through 2.23 (inclusive), then destructors for `thread_local` variables (of the calling thread) will be called by `quick_exit`. They will not be called in other cases.|
|`qsort_r`|The polyfill implementation is _some_ in-place non-stable comparison-based sort with O(n log n) worst-case behaviour. The exact sequence of element-wise compares and swaps may differ to that of any particular glibc version. If the comparison function does not define a strict total order on the elements being sorted, then the result may differ to that of any particular glibc version (notably, this includes the case where the array being sorted contains elements that compare equal but are not bitwise identical).|
|`sem_clockwait`|If the ambient glibc lacks `sem_clockwait`, then the polyfill silently remaps `sem_clockwait` to `sem_timedwait`, thereby always waiting against `CLOCK_REALTIME`.|
|`stdc_bit_ceil_uc`, `stdc_bit_ceil_us`|The standard leaves the return value undefined if it does not fit in the return type. The polyfill implementation follows the glibc approach of returning the correct result as an int (which will become zero if truncated to the return type).|
|`stdc_bit_ceil_ui`, `stdc_bit_ceil_ul`, `stdc_bit_ceil_ull`|The standard leaves the return value undefined if it does not fit in the return type. The polyfill implementation follows the glibc approach of returning zero.|
