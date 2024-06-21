// C11 <threads.h> functions.

extern function pthread_cond_broadcast linkname("libpthread.so.0::pthread_cond_broadcast@GLIBC_2.17");

public function cnd_broadcast {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  bl pthread_cond_broadcast
  b thrd_err_remap
}

extern function pthread_cond_init linkname("libpthread.so.0::pthread_cond_init@GLIBC_2.17");

public function cnd_init {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  mov x1, #0
  bl pthread_cond_init
  b thrd_err_remap
}

extern function pthread_cond_signal linkname("libpthread.so.0::pthread_cond_signal@GLIBC_2.17");

public function cnd_signal {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  bl pthread_cond_signal
  b thrd_err_remap
}

extern function pthread_cond_timedwait linkname("libpthread.so.0::pthread_cond_timedwait@GLIBC_2.17");

public function cnd_timedwait {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  bl pthread_cond_timedwait
  b thrd_err_remap
}

extern function pthread_cond_wait linkname("libpthread.so.0::pthread_cond_wait@GLIBC_2.17");

public function cnd_wait {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  bl pthread_cond_wait
  b thrd_err_remap
}

extern function pthread_mutexattr_init linkname("libpthread.so.0::pthread_mutexattr_init@GLIBC_2.17");
extern function pthread_mutexattr_settype linkname("libpthread.so.0::pthread_mutexattr_settype@GLIBC_2.17");
extern function pthread_mutex_init linkname("libpthread.so.0::pthread_mutex_init@GLIBC_2.17");

public function mtx_init {
  paciasp.cfi
  stp.cfi x0, x1, [sp, #-0x30]!
  stp.cfi fp, lr, [sp, #0x20]
  add.cfi fp, sp, #0x20
  // Initialise a pthread_mutexattr_t.
  add x0, sp, #0x10
  bl pthread_mutexattr_init
  // Remap 1,3 -> 1; anything else -> 0, passing this to pthread_mutexattr_settype.
  add x0, sp, #0x10
  ldr x1, [sp, #8]
  and w1, w1, #-3
  cmp w1, #1
  csinc w1, wzr, wzr, ne
  bl pthread_mutexattr_settype
  // Now call pthread_mutex_init with our pthread_mutexattr_t.
  ldr x0, [sp]
  add x1, sp, #0x10
  bl pthread_mutex_init
  // Usual tail.
  mov sp, fp
  b thrd_err_remap
}

extern function pthread_mutex_lock linkname("libpthread.so.0::pthread_mutex_lock@GLIBC_2.17");

public function mtx_lock {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  bl pthread_mutex_lock
  b thrd_err_remap
}

extern function pthread_mutex_timedlock linkname("libpthread.so.0::pthread_mutex_timedlock@GLIBC_2.17");

public function mtx_timedlock {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  bl pthread_mutex_timedlock
  b thrd_err_remap
}

extern function pthread_mutex_trylock linkname("libpthread.so.0::pthread_mutex_trylock@GLIBC_2.17");

public function mtx_trylock {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  bl pthread_mutex_trylock
  b thrd_err_remap
}

extern function pthread_mutex_unlock linkname("libpthread.so.0::pthread_mutex_unlock@GLIBC_2.17");

public function mtx_unlock {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  bl pthread_mutex_unlock
  b thrd_err_remap
}

extern function pthread_create linkname("libpthread.so.0::pthread_create@GLIBC_2.17");

public function thrd_create {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  mov x3, x2
  mov x2, x1
  mov x1, #-1
  bl pthread_create
  b thrd_err_remap
}

extern function pthread_detach linkname("libpthread.so.0::pthread_detach@GLIBC_2.17");

public function thrd_detach {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  bl pthread_detach
  b thrd_err_remap
}

extern function pthread_exit linkname("libpthread.so.0::pthread_exit@GLIBC_2.17");

public function thrd_exit {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  sxtw x0, w0
  b pthread_exit
}

extern function pthread_join linkname("libpthread.so.0::pthread_join@GLIBC_2.17");

public function thrd_join {
  paciasp.cfi
  str.cfi x1, [sp, #-0x20]!
  stp.cfi fp, lr, [sp, #0x10]
  add.cfi fp, sp, #0x10
  add x1, sp, #8
  bl pthread_join
  ldp.cfi x1, x2, [sp], #0x10
  cbz x1, thrd_err_remap
  str w2, [x1]
  b thrd_err_remap
}

extern function clock_nanosleep linkname("libc.so.6::clock_nanosleep@GLIBC_2.17");

public function thrd_sleep {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  mov x3, x1
  mov x2, x0
  mov x1, #0
  mov x0, #0
  bl clock_nanosleep
  cbz w0, tail
  cmp w0, #4
  csinc w0, wzr, wzr, ne
  sub w0, w0, #2
tail:
  ldp.cfi fp, lr, [sp], #0x10
  autiasp.cfi
  ret.cfi
}

public function thrd_yield {
  bti c
  mov x8, #0x7c
  svc #0
  ret.cfi
}

extern function pthread_key_create linkname("libpthread.so.0::pthread_key_create@GLIBC_2.17");

public function tss_create {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  bl pthread_key_create
  b thrd_err_remap
}

extern function pthread_setspecific linkname("libpthread.so.0::pthread_setspecific@GLIBC_2.17");

public function tss_set {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  bl pthread_setspecific
  b thrd_err_remap
}

function thrd_err_remap {
  // 0 -> thrd_success (0)
  // ENOMEM (12) -> thrd_nomem (3)
  // ETIMEDOUT (110) -> thrd_timedout (4)
  // EBUSY (16) -> thrd_busy (1)
  // default -> thrd_error (2)
  cfi_byte 0x2d, 0x0c, 31, 16, 0x9d, 2, 0x9e, 1 // DW_CFA_GNU_window_save, DW_CFA_def_cfa(sp, +16), DW_CFA_offset(fp, -2), DW_CFA_offset(lr, -1)
  ldp fp, lr, [sp], #0x10
  cfi_byte 0x0e, 0, 0xdd, 0xde // DW_CFA_def_cfa_offset(0), DW_CFA_restore(fp), DW_CFA_restore(lr)
  autiasp
  cfi_byte 0x2d // DW_CFA_GNU_window_save
  cbz w0, done             //   0 -> 0
  mov w1, #0x030c          //  12 -> 3
  movk w1, #0x0110 lsl #16 //  16 -> 1
  movk x1, #0x046e lsl #32 // 110 -> 4
  and w2, w0, #0x30
  lsr x1, x1, x2
  cmp w0, w1 uxtb
  mov w0, #2               // default -> 2
  ubfm w1, w1, #8, #15
  csel w0, w1, w0, eq
done:
  ret
}
