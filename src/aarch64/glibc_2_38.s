extern function strlen linkname("libc.so.6::strlen@GLIBC_2.17");
extern function strnlen linkname("libc.so.6::strnlen@GLIBC_2.17");
extern function memcpy linkname("libc.so.6::memcpy@GLIBC_2.17");

public function __strlcat_chk {
  bti c
  cmp x3, x2
  b.hs strlcat
  b __chk_fail
}

public function strlcat {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x30]!
  mov fp, sp
  stp.cfi x20, x21, [sp, #0x10]
  stp.cfi x22, x23, [sp, #0x20]

  mov x20, x0
  mov x21, x1
  mov x22, x2
  mov x0, x1
  bl strlen

  cbz x22, done
  mov x23, x0 // strlen(src)
  mov x0, x20
  mov x1, x22
  bl strnlen

  mov x1, x21
  mov x21, x0 // strnlen(dst)

  subs x2, x22, x0
  b.eq no_memcpy
  sub x2, x2, #1
  cmp x2, x23
  csel x2, x2, x23, lo
  add x0, x20, x21
  add x20, x0, x2
  bl memcpy
  strb wzr, [x20]
no_memcpy:
  add x0, x21, x23
done:
  ldp.cfi x22, x23, [sp, #0x20]
  ldp.cfi x20, x21, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x30
  autiasp.cfi
  ret.cfi
}

public function __strlcpy_chk {
  bti c
  cmp x3, x2
  b.hs strlcpy
  b __chk_fail
}

public function strlcpy {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x30]!
  mov fp, sp
  stp.cfi x20, x21, [sp, #0x10]
  str.cfi x22, [sp, #0x20]

  mov x20, x0
  mov x21, x1
  mov x22, x2
  mov x0, x1
  bl strlen

  subs x2, x22, #1
  b.lo done
  cmp x2, x0
  csel x2, x2, x0, lo
  mov x22, x0
  mov x0, x20
  mov x1, x21
  mov x21, x2
  bl memcpy
  strb wzr, [x20, x21]
  mov x0, x22

done:
  ldr.cfi x22, [sp, #0x20]
  ldp.cfi x20, x21, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x30
  autiasp.cfi
  ret.cfi
}

extern function wcslen linkname("libc.so.6::wcslen@GLIBC_2.17");
extern function wcsnlen linkname("libc.so.6::wcsnlen@GLIBC_2.17");

public function __wcslcat_chk {
  bti c
  cmp x3, x2
  b.hs wcslcat
  b __chk_fail
}

public function wcslcat {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x30]!
  mov fp, sp
  stp.cfi x20, x21, [sp, #0x10]
  stp.cfi x22, x23, [sp, #0x20]

  mov x20, x0
  mov x21, x1
  mov x22, x2
  mov x0, x1
  bl wcslen

  cbz x22, done
  mov x23, x0 // wcslen(src)
  mov x0, x20
  mov x1, x22
  bl wcsnlen

  mov x1, x21
  mov x21, x0 // wcsnlen(dst)

  subs x2, x22, x0
  b.eq no_memcpy
  sub x2, x2, #1
  cmp x2, x23
  csel x2, x2, x23, lo
  add x0, x20, x21 lsl #2
  lsl x2, x2, #2
  add x20, x0, x2
  bl memcpy
  str wzr, [x20]
no_memcpy:
  add x0, x21, x23
done:
  ldp.cfi x22, x23, [sp, #0x20]
  ldp.cfi x20, x21, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x30
  autiasp.cfi
  ret.cfi
}

public function __wcslcpy_chk {
  bti c
  cmp x3, x2
  b.hs wcslcpy
  b __chk_fail
}

public function wcslcpy {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x30]!
  mov fp, sp
  stp.cfi x20, x21, [sp, #0x10]
  str.cfi x22, [sp, #0x20]

  mov x20, x0
  mov x21, x1
  mov x22, x2
  mov x0, x1
  bl wcslen

  subs x2, x22, #1
  b.lo done
  cmp x2, x0
  csel x2, x2, x0, lo
  mov x22, x0
  mov x0, x20
  mov x1, x21
  lsl x2, x2, #2
  mov x21, x2
  bl memcpy
  str wzr, [x20, x21]
  mov x0, x22

done:
  ldr.cfi x22, [sp, #0x20]
  ldp.cfi x20, x21, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x30
  autiasp.cfi
  ret.cfi
}
