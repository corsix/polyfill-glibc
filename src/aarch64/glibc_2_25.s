extern function bzero linkname("libc.so.6::bzero@GLIBC_2.17");
extern function __chk_fail linkname("libc.so.6::__chk_fail@GLIBC_2.17");

public function __explicit_bzero_chk {
  bti c
  cmp x2, x1
  b.cc __chk_fail
  b bzero
}
