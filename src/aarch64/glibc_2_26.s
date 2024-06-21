extern function realloc linkname("libc.so.6::realloc@GLIBC_2.17");

public function reallocarray {
  bti c
  umulh x3, x1, x2
  mul x1, x1, x2
  cbz x3, realloc
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  bl __errno_location
  ldp.cfi fp, lr, [sp], #0x10
  mov w1, #12
  str w1, [x0]
  autiasp.cfi
  mov x0, #0
  ret.cfi
}
