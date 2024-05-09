extern function malloc linkname("libc.so.6::malloc@GLIBC_2.2.5");

public function __sched_cpualloc {
  endbr64
  add rdi, 63
  shr rdi, 6
  shl rdi, 3
  jmp malloc
}

extern function open64 linkname("libc.so.6::open64@GLIBC_2.2.5");

const variable __open64_2_fail_msg {
  asciiz "*** invalid open64 call: O_CREAT or O_TMPFILE without mode ***: terminated\n"
}

public function __open64_2 {
  endbr64
  mov eax, esi
  and eax, 0x410040
  jnp fail
  cmp eax, 0x410000
  jz fail
  xor eax, eax
  jmp open64
fail:
  lea rdi, __open64_2_fail_msg
  jmp libc_fatal
}
