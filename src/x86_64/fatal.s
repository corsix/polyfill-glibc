extern function abort linkname("libc.so.6::abort@GLIBC_2.2.5");

const variable __chk_fail_msg {
  asciiz "*** buffer overflow detected ***: terminated\n"
}

extern function __chk_fail_extern linkname("libc.so.6::__chk_fail@GLIBC_2.3.4");

public function __chk_fail {
  endbr64
  lea rdi, __chk_fail_msg
  jmp libc_fatal
}

const variable __stack_chk_fail_msg {
  asciiz "*** stack smashing detected ***: terminated\n"
}

public function __stack_chk_fail {
  endbr64
  lea rdi, __stack_chk_fail_msg
  jmp libc_fatal
}

function libc_fatal {
  // String in rdi. No vararg support here.
  // Step 1: Compute string length.
  mov rsi, rdi
  xor eax, eax
  lea ecx, [eax - 1]
  repne scasb
  neg ecx
  lea edx, [ecx - 2]
  // Step 2: Write string to stderr.
  lea edi, [eax + 2]
write_loop:
  sub edx, eax
  jz write_done
  add rsi, rax
write_loop_no_add:
  mov eax, 1
  syscall
  test rax, rax
  jg write_loop
  cmp eax, -4 // EINTR
  jz write_loop_no_add
write_done:
  // Step 3: Abort.
  jmp abort
}
