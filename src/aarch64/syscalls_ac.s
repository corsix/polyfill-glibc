// See ../../docs/Asynchronous_cancellation.md

extern function pthread_setcanceltype linkname("libc.so.6::pthread_setcanceltype@GLIBC_2.17");

variable preadv2_2_26_impl_ptr {
  qword 0
}

const variable preadv2_2_26_descriptor {
  relative_offset_i32 preadv2_2_26_impl_ptr
  byte '2', '6'
  asciiz "preadv2"
}

public function preadv2_2_26 {
  bti c
  ldr x17, [preadv2_2_26_impl_ptr]
  cbz x17, determine_impl
  br x17
our_impl_ac:
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x30]!
  mov fp, sp
  stp w0, w2, [sp, #0x10]
  str w4, [sp, #0x18]
  stp x1, x3, [sp, #0x20]
  mov x0, #1
  add x1, sp, #0x1c
  bl pthread_setcanceltype
  ldpsw x0, x2, [sp, #0x10]
  ldrsw x5, [sp, #0x18]
  ldp x1, x3, [sp, #0x20]
  lsr x4, x3, #32
  mov x8, #0x11e // __NR_preadv2
  svc #0
  cmp x0, #-38 // ENOSYS
  b.ne done_emulate
  mov x0, #-95 // ENOTSUP
  cbnz w5, done_emulate
  cmp x3, #-1
  mov w8, #0x45 // __NR_preadv
  mov w0, #0x41 // __NR_readv
  csel w8, w8, w0, ne
  ldrsw x0, [sp, #0x10]
  svc #0
done_emulate:
  str x0, [sp, #0x10]
  ldr w0, [sp, #0x1c]
  add x1, sp, #0x1c
  bl pthread_setcanceltype
  ldr x0, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x30
  autiasp.cfi
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
determine_impl:
  adr x16, preadv2_2_26_descriptor
  adr x17, our_impl_ac
  b dlvsym_for_ac_syscall
}

variable pwritev2_2_26_impl_ptr {
  qword 0
}

const variable pwritev2_2_26_descriptor {
  relative_offset_i32 pwritev2_2_26_impl_ptr
  byte '2', '6'
  asciiz "pwritev2"
}

public function pwritev2_2_26 {
  bti c
  ldr x17, [pwritev2_2_26_impl_ptr]
  cbz x17, determine_impl
  br x17
our_impl_ac:
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x30]!
  mov fp, sp
  stp w0, w2, [sp, #0x10]
  str w4, [sp, #0x18]
  stp x1, x3, [sp, #0x20]
  mov x0, #1
  add x1, sp, #0x1c
  bl pthread_setcanceltype
  ldpsw x0, x2, [sp, #0x10]
  ldrsw x5, [sp, #0x18]
  ldp x1, x3, [sp, #0x20]
  lsr x4, x3, #32
  mov x8, #0x11f // __NR_pwritev2
  svc #0
  cmp x0, #-38 // ENOSYS
  b.ne done_emulate
  mov x0, #-95 // ENOTSUP
  cbnz w5, done_emulate
  cmp x3, #-1
  mov w8, #0x46 // __NR_pwritev
  mov w0, #0x42 // __NR_writev
  csel w8, w8, w0, ne
  ldrsw x0, [sp, #0x10]
  svc #0
done_emulate:
  str x0, [sp, #0x10]
  ldr w0, [sp, #0x1c]
  add x1, sp, #0x1c
  bl pthread_setcanceltype
  ldr x0, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x30
  autiasp.cfi
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
determine_impl:
  adr x16, pwritev2_2_26_descriptor
  adr x17, our_impl_ac
  b dlvsym_for_ac_syscall
}

variable copy_file_range_2_27_impl_ptr {
  qword 0
}

const variable copy_file_range_2_27_descriptor {
  relative_offset_i32 copy_file_range_2_27_impl_ptr
  byte '2', '7'
  asciiz "copy_file_range"
}

public function copy_file_range_2_27 {
  bti c
  ldr x17, [copy_file_range_2_27_impl_ptr]
  cbz x17, determine_impl
  br x17
our_impl_ac:
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x40]!
  mov fp, sp
  stp w0, w2, [sp, #0x10]
  stp x1, x3, [sp, #0x20]
  stp x4, x5, [sp, #0x30]
  mov x0, #1
  add x1, sp, #0x18
  bl pthread_setcanceltype
  ldpsw x0, x2, [sp, #0x10]
  ldp x1, x3, [sp, #0x20]
  ldp x4, x5, [sp, #0x30]
  mov x8, #0x11d
  svc #0
  str x0, [sp, #0x10]
  ldr w0, [sp, #0x18]
  add x1, sp, #0x18
  bl pthread_setcanceltype
  ldr x0, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x40
  autiasp.cfi
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
determine_impl:
  adr x16, copy_file_range_2_27_descriptor
  adr x17, our_impl_ac
  b dlvsym_for_ac_syscall
}

variable fcntl64_2_28_impl_ptr {
  qword 0
}

const variable fcntl64_2_28_descriptor {
  relative_offset_i32 fcntl64_2_28_impl_ptr
  byte '2', '8'
  asciiz "fcntl64"
}

public function fcntl64_2_28 {
  bti c
  ldr x17, [fcntl64_2_28_impl_ptr]
  cbz x17, determine_impl
  br x17
our_impl:
  bti c
  sxtw x0, w0
  cmp w1, #9
  b.eq our_impl_getown
  cmp w1, #38
  ccmp w1, #7, #4, ne
  b.eq our_impl_ac
  // Not in any of the hard cases; just do a syscall.
  sxtw x1, w1
  mov x8, #0x19
  svc #0
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
our_impl_getown:
  // F_GETOWN has an ambiguous return value, so we use F_GETOWN_EX instead.
  sub.cfi sp, sp, #0x10
  mov x1, #16
  mov x2, sp
  mov x8, #0x19
  svc #0
  ldp.cfi w1, w2, [sp], #0x10
  cmn x0, #0x1000
  b.hi syscall_errno
  cmp w1, #2
  csneg w0, w2, w2, ne
  ret.cfi
our_impl_ac:
  // F_SETLKW / F_OFD_SETLKW can block, and are thus defined as cancellation points.
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x30]!
  mov fp, sp
  stp x0, x2, [sp, #0x10]
  str w1, [sp, #0x20]
  mov x0, #1
  add x1, sp, #0x28
  bl pthread_setcanceltype
  ldp x0, x2, [sp, #0x10]
  ldrsw x1, [sp, #0x20]
  mov x8, #0x19
  svc #0
  str x0, [sp, #0x10]
  ldr w0, [sp, #0x28]
  add x1, sp, #0x28
  bl pthread_setcanceltype
  ldr x0, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x30
  autiasp.cfi
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
determine_impl:
  adr x16, fcntl64_2_28_descriptor
  adr x17, our_impl
  b dlvsym_for_ac_syscall
}

variable epoll_pwait2_2_35_impl_ptr {
  qword 0
}

const variable epoll_pwait2_2_35_descriptor {
  relative_offset_i32 epoll_pwait2_2_35_impl_ptr
  byte '3', '5'
  asciiz "epoll_pwait2"
}

public function epoll_pwait2_2_35 {
  bti c
  ldr x17, [epoll_pwait2_2_35_impl_ptr]
  cbz x17, determine_impl
  br x17
our_impl_ac:
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x40]!
  mov fp, sp
  stp w0, w2, [sp, #0x10]
  stp x1, x3, [sp, #0x20]
  str x4, [sp, #0x30]
  mov x0, #1
  add x1, sp, #0x18
  bl pthread_setcanceltype
  ldpsw x0, x2, [sp, #0x10]
  ldp x1, x3, [sp, #0x20]
  ldr x4, [sp, #0x30]
  mov x5, #8
  mov x8, #0x1b9
  svc #0
  str x0, [sp, #0x10]
  ldr w0, [sp, #0x18]
  add x1, sp, #0x18
  bl pthread_setcanceltype
  ldr x0, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x40
  autiasp.cfi
  cmn x0, #0x1000
  b.hi syscall_errno
  ret.cfi
determine_impl:
  adr x16, epoll_pwait2_2_35_descriptor
  adr x17, our_impl_ac
  b dlvsym_for_ac_syscall
}

const variable str_GLIBC_2_ {
  qword '.2_CBILG'
}

function dlvsym_for_ac_syscall {
  // Result/version/name struct in x16, fallback impl in x17.
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x60]!
  mov fp, sp
  stp x0, x1, [sp, #0x10]
  stp x2, x3, [sp, #0x20]
  stp x4, x5, [sp, #0x30]
  stp x16, x17, [sp, #0x40]
  add x2, sp, #0x50
  ldr x0, [str_GLIBC_2_]
  str x0, [x2]
  ldrh w0, [x16, #4]
  str w0, [x2, #8]
  add x1, x16, #6
  mov x0, #0
  bl dlvsym
  ldp x16, x17, [sp, #0x40]
  cmp x0, #0
  csel x17, x17, x0, eq
  ldrsw x0, [x16]
  cbz x0, no_store
  str x17, [x16, x0]
no_store:
  ldp x0, x1, [sp, #0x10]
  ldp x2, x3, [sp, #0x20]
  ldp x4, x5, [sp, #0x30]
  ldp.cfi fp, lr, [sp], #0x60
  autiasp.cfi
  br x17
}

extern function dlvsym linkname("libc.so.6::dlvsym@GLIBC_2.34"); // Likely renamed to libdl.so.2::dlvsym@GLIBC_2.17
