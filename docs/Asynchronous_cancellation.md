## Asynchronous cancellation

The [`pthread_cancel`](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_cancel.html) function is defective by design and should not be used. See [How to stop Linux threads cleanly](https://mazzo.li/posts/stopping-linux-threads.html) for a tour of the problem and various better solutions. That said, some applications still (misguidedly) make use of `pthread_cancel`, and some special caveats apply when polyfilling said applications.

## What does `pthread_cancel` do?

Every thread has three pieces of state:

|Variable name|Possible values|Initial value|Changed by|
|-------------|---------------|-------------|----------|
|Cancel state|ENABLE, DISABLE|ENABLE|[`pthread_setcancelstate`](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_setcancelstate.html)|
|Cancel type|DEFERRED, ASYNCHRONOUS|DEFERRED|[`pthread_setcanceltype`](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_setcancelstate.html)|
|Cancel requested|NO, YES|NO|[`pthread_cancel`](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_cancel.html) (always changes to YES)|

A thread will be terminated (possibly with cleanup functions / destructors being called), if either of the following state combinations occur:
1. _Cancel requested_ is YES, _Cancel state_ is ENABLE, _Cancel type_ is ASYNCHRONOUS.
2. _Cancel requested_ is YES, _Cancel state_ is ENABLE, _Cancel type_ is DEFERRED, and the thread calls a libc function defined as a cancellation point.

Roughly speaking, every libc function that _could_ block execution for a non-trivial amount of time is defined as a cancellation point. This includes, but is not limited to, the following functions:
* `accept`
* `close`
* `connect`
* `copy_file_range`
* `epoll_wait` / `epoll_pwait` / `epoll_pwait2`
* `fcntl` (when called with `F_SETLKW` or `F_OFD_SETLKW`)
* `fsync` / `fdatasync` / `sync_file_range` / `msync`
* `getrandom`
* `open` / `openat` / `open_by_handle_at`
* `pause` / `sigpause` / `sigsuspend`
* `pthread_testcancel`
* `read` / `readv` / `pread`
* `recv` / `recvfrom` / `recvmsg` / `recvmmsg` / `msgrcv` / `mq_receive` / `mq_timedreceive`
* `send` / `sendto` / `sendmsg` / `sendmmsg` / `msgsnd` / `mq_send` / `mq_timedsend`
* `sigwait` / `sigwaitinfo` / `sigtimedwait`
* `sleep` / `usleep` / `nanosleep` / `clock_nanosleep`
* `write` / `writev` / `pwrite`

## glibc semantics of cancellation points

Taking `epoll_pwait2` as an example, at the time of writing, the glibc implementation of `epoll_pwait2` is roughly:

```c
old_type = pthread_setcanceltype(ASYNCHRONOUS);
result = syscall(epoll_pwait2, ...);
pthread_setcanceltype(old_type);
check_for_pthread_cancel_race();
return result;
```

When `pthread_cancel` is called, if the target thread has _Cancel state_ of ENABLE and _Cancel type_ of ASYNCHRONOUS, then a signal is sent to the target thread. Shortly thereafter, the signal will be received by the target thread, wherein the signal handler confirms that _Cancel state_ is still ENABLE and _Cancel type_ is still ASYNCHRONOUS. If so, the thread will be terminated. If not, the signal handler will do nothing (though delivery of the signal might cause an `EINTR` result from an unrelated syscall that was being made at the time of delivery).

There are (at least) two issues with this implementation:
1. `pthread_cancel` could send the signal _before_ `pthread_setcanceltype(old_type)`, but the signal might arrive _after_ `pthread_setcanceltype(old_type)`.
2. A signal (unrelated to cancellation) could be delivered during the `syscall`, and the handler for that signal could perform a `longjmp`, thereby causing `pthread_setcanceltype(old_type)` to not be called.

The 1<sup>st</sup> point is addressed by the `check_for_pthread_cancel_race` call: yet another piece of per-thread state tracks whether a `pthread_cancel` call is in the middle of sending a signal, and if so, `check_for_pthread_cancel_race` waits for the signal delivery, thereby avoiding it from causing `EINTR` on an unrelated syscall.

The 2<sup>nd</sup> point is unaddressed. It is tracked as part of [glibc bug 12683](https://sourceware.org/bugzilla/show_bug.cgi?id=12683), where a solution has been proposed, but not yet implemented.

## Polyfilled semantics of cancellation points

For functions that don't meaningfully block in practice, such as `open_by_handle_at` and `getrandom`, the polyfill implementation of these functions inserts a `pthread_testcancel` call, as in:
```c
pthread_testcancel();
return syscall(open_by_handle_at, ...);
```

For functions that really can block, the polyfill implementation takes a two-pronged strategy:
1. If the ambient glibc provides the function being polyfilled, call that.
2. Otherwise, implement it similarly to how glibc currently does, albeit without the `check_for_pthread_cancel_race` call:

   ```c
   old_type = pthread_setcanceltype(ASYNCHRONOUS);
   result = syscall(epoll_pwait2, ...);
   pthread_setcanceltype(old_type);
   return result;
   ```

If glibc one day fixes [bug 12683](https://sourceware.org/bugzilla/show_bug.cgi?id=12683), then using the ambient glibc implementation will give bug-free behaviour (when running with a sufficiently new glibc). On older glibc versions, the polyfill implementation is only marginally worse than the glibc implementation: the lack of a `check_for_pthread_cancel_race` call is unfortunate, but applications should be prepared to handle `EINTR` anyway.
