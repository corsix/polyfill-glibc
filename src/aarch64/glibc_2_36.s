public function arc4random_uniform {
  // https://dotat.at/@/2022-04-20-really-divisionless.html
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x30]!
  mov fp, sp
  stp.cfi x20, x21, [sp, #0x10]
  str.cfi x22, [sp, #0x20]
  mov w20, w0
  add x0, sp, #0x28
  mov x1, #4
  bl arc4random_buf
  ldr w0, [sp, #0x28]
  umull x0, w0, w20
  lsr x21, x0, #32 // save high part, either it or it plus one will be our result
loop:
  orn w22, wzr, w0
  cmp w20, w22
  b.ls done
  add x0, sp, #0x28
  mov x1, #4
  bl arc4random_buf
  ldr w0, [sp, #0x28]
  umull x0, w0, w20
  cmp x22, x0 lsr #32
  b.eq loop
  csinc w21, w21, w21, hs // add one if w22 < (x0 lsr #32)
done:
  mov w0, w21
  ldr.cfi x22, [sp, #0x20]
  ldp.cfi x20, x21, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x30
  autiasp.cfi
  ret.cfi
}

public function arc4random {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x20]!
  mov fp, sp
  mov x1, #4
  add x0, sp, #0x10
  bl arc4random_buf
  ldr w0, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x20
  autiasp.cfi
  ret.cfi
}

variable arc4random_buf_done_init {
  dword 0
}

const variable str_dev_urandom {
  asciiz "/dev/urandom"
}

const variable str_dev_random {
  asciiz "/dev/random"
}

const variable arc4random_buf_fail_msg {
  asciiz "Fatal glibc error: cannot get entropy for arc4random\n"
}

public function arc4random_buf {
  bti c
  // Start by trying the getrandom syscall.
  add x13, x0, x1
  mov x0, #0
  mov x2, #0
  mov x8, #0x116 // __NR_getrandom
try_getrandom:
  subs x1, x1, x0
  b.eq done
try_getrandom_no_add:
  sub x0, x13, x1
  svc #0
  cmp x0, #0
  b.gt try_getrandom
  cmp w0, #-4 // EINTR
  b.eq try_getrandom_no_add
  cmp w0, #-38 // ENOSYS
  b.ne hard_fail

  mov x11, x1
  // Ensure we've checked for /dev/urandom initialisation.
  ldr w0, [arc4random_buf_done_init]
  cbz w0, do_init
done_init:

  // Open /dev/urandom, read from it, close it.
  adr x1, str_dev_urandom
  mov x2, #0x80000
  movk x2, #0x0100 // O_RDONLY | O_CLOEXEC | O_NOCTTY
  mov x3, #0
  mov x8, #0x38 // __NR_openat
try_open:
  mov x0, #-100
  svc #0
  cmp x0, #-4 // EINTR
  b.eq try_open
  cmn x0, #0x1000
  b.hi hard_fail
  mov x2, x11 // length
  mov w11, w0 // fd
  mov x0, #0
  mov x8, #0x3f // __NR_read
try_read:
  subs x2, x2, x0
  b.eq done_read
try_read_no_add:
  sub x1, x13, x2
  mov w0, w11
  svc #0
  cmp x0, #0
  b.gt try_read
  cmp w0, #-4 // EINTR
  b.eq try_read_no_add
hard_fail:
  adr x0, arc4random_buf_fail_msg
  b libc_fatal
done_read:
  mov x8, #0x39 // __NR_close
  mov w0, w11
  svc #0
done:
  ret.cfi

  // Open /dev/random, poll it for readability, close it.
do_init:
  adr x1, str_dev_random
  mov x2, #0x80000
  movk x2, #0x0100 // O_RDONLY | O_CLOEXEC | O_NOCTTY
  mov x3, #0
  mov x8, #0x38 // __NR_openat
try_init_open:
  mov x0, #-100
  svc #0
  cmp x0, #-4 // EINTR
  b.eq try_init_open
  cmn x0, #0x1000
  b.hi hard_fail

  mov w1, #1
  stp.cfi w0, w1, [sp, #-0x10]!
  mov x8, #0x49 // __NR_ppoll
  mov x2, #0
  mov x3, #0
  mov x4, #0
try_poll:
  mov x0, sp
  svc #0
  cmp x0, #-4 // EINTR
  b.eq try_poll
  cmn x0, #0x1000
  ldr.cfi w0, [sp], #0x10
  b.hi hard_fail
  mov x8, #0x39 // __NR_close
  svc #0
  adr x0, arc4random_buf_done_init
  str w8, [x0]
  b done_init
}
