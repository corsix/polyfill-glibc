extern function bzero linkname("libc.so.6::bzero@GLIBC_2.2.5");

public function __explicit_bzero_chk {
  endbr64
  cmp rsi, rdx
  ja fail
  jmp bzero
fail:
  jmp __chk_fail_extern
}
