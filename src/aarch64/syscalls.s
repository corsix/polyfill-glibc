public function getentropy {
  bti c
  cmp x1, #0x100
  b.hi err_io
  add x3, x0, x1
  mov x0, #0
  mov x2, #0
  mov x8, #0x116 // __NR_getrandom
loop:
  subs w1, w1, w0
  b.eq done
loop_no_add:
  sub x0, x3, x1
  svc #0
  cmp x0, #0
  b.gt loop
  cmp w0, #-4 // EINTR
  b.eq loop_no_add
  cbnz w0, syscall_errno
err_io:
  mov w0, #-5
  b syscall_errno
done:
  mov x0, #0
  ret.cfi
}

extern function pthread_testcancel linkname("libpthread.so.0::pthread_testcancel@GLIBC_2.17");

public function getrandom {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x20]!
  str.cfi x20, [sp, 0x10]
  mov.cfi fp, sp
  mov w2, w2
  mov x8, #0x116 // __NR_getrandom
  svc #0
  mov x20, x0
  bl pthread_testcancel
  cmn x20, #0x1000
  b.ls done
  neg w20, w20
  bl __errno_location
  str w20, [x0]
  mov x20, #-1
done:
  mov x0, x20
  ldr.cfi x20, [sp, 0x10]
  ldp.cfi fp, lr, [sp], #0x20
  autiasp.cfi
  ret.cfi
}

public function memfd_create {
  bti c
  mov x8, #0x117
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function mlock2 {
  bti c
  cbz w2, no_flags
  mov x8, #0x11c
  svc #0
  cmn x0, #0x1000
  b.hi error
  ret.cfi
error:
  cmp w0, #-38
  mov w1, #-22
  csel w0, w1, w0, eq
  b syscall_errno
no_flags:
  mov x8, #0xe4
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function pkey_alloc {
  bti c
  mov x8, #0x121
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function pkey_free {
  bti c
  mov x8, #0x122
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function pkey_mprotect {
  bti c
  mov x8, #0xe2
  cmn w3, #-1
  b.eq do_syscall
  sxtw x2, w2
  sxtw x3, w3
  mov x8, #0x120
do_syscall:
  svc #0
  b.hi syscall_errno
  ret.cfi
}

public function renameat2 {
  bti c
  sxtw x0, w0
  sxtw x2, w2
  ands w4, w4, w4
  b.eq no_flags
  mov x8, #0x114
  svc #0
  b.hi error
  ret.cfi
error:
  cmp w0, #-38
  mov w1, #-22
  csel w0, w1, w0, eq
  b syscall_errno
no_flags:
  mov x8, #0x26
  svc #0
  b.hi syscall_errno
  ret.cfi
}

public function getdents64 {
  bti c
  tst x2, #0xffffffff80000000
  csinv w2, w2, wzr, eq
  sxtw x0, w0
  and w2, w2, #0x7fffffff
  mov x8, #0x3d
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function gettid {
  bti c
  mov x8, #0xb2
  svc #0
  ret.cfi
}

public function tgkill {
  bti c
  mov x8, #0x83
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function fstat {
  bti c
  sxtw x0, w0
  mov x8, #0x50
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function fstatat {
  bti c
  sxtw x0, w0
  sxtw x3, w3
  mov x8, #0x4f
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  mov x0, #0
  ret.cfi
}

public function lstat {
  bti c
  mov x3, #0x100
  mov x2, x1
  mov x1, x0
  mov x0, #-100
  mov x8, #0x4f
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  mov x0, #0
  ret.cfi
}

public function stat {
  bti c
  mov x3, #0
  mov x2, x1
  mov x1, x0
  mov x0, #-100
  mov x8, #0x4f
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  mov x0, #0
  ret.cfi
}

public function mknod {
  bti c
  tst x2, #0xffffffff00000000
  b.ne syscall_EINVAL
  mov x3, x2
  mov w2, w1
  sxtw x1, w0
  mov x0, #-100
  mov x8, #0x21
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function mknodat {
  bti c
  tst x3, #0xffffffff00000000
  b.ne syscall_EINVAL
  sxtw x0, w0
  mov w2, w2
  mov x8, #0x21
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function close_range {
  bti c
  mov x8, #0x1b4
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function execveat {
  bti c
  sxtw x0, w0
  sxtw x4, w4
  mov x8, #0x119
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function fsconfig {
  bti c
  mov x8, #0x1af
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function fsmount {
  bti c
  mov x8, #0x1b0
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function fsopen {
  bti c
  mov x8, #0x1ae
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function fspick {
  bti c
  mov x8, #0x1b1
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function mount_setattr {
  bti c
  mov x8, #0x1ba
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function move_mount {
  bti c
  mov x8, #0x1ad
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function open_tree {
  bti c
  mov x8, #0x1ac
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function pidfd_getfd {
  bti c
  mov x8, #0x1b6
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function pidfd_open {
  bti c
  mov x8, #0x1b2
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function pidfd_send_signal {
  bti c
  mov x8, #0x1a8
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function process_madvise {
  bti c
  mov x8, #0x1b8
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

public function process_mrelease {
  bti c
  mov x8, #0x1c0
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
}

extern function __errno_location linkname("libc.so.6::__errno_location@GLIBC_2.17");

function syscall_EBADF {
  mov w0, #-9
  b syscall_errno
}

function syscall_EINVAL {
  mov w0, #-22
  b syscall_errno
}

public function syscall_ENOSYS {
  bti c
  mov w0, #-38
  b syscall_errno
}

function syscall_errno {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x20]!
  mov fp, sp
  str w0, [sp, 0x10]
  bl __errno_location
  ldr w1, [sp, 0x10]
  neg w1, w1
  str w1, [x0]
  mov x0, #-1
  ldp.cfi fp, lr, [sp], #0x20
  autiasp.cfi
  ret.cfi
}
