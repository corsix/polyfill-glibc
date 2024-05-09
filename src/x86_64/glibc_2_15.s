const variable __fdelt_chk_fail_msg {
  asciiz "*** bit out of range 0 - FD_SETSIZE on fd_set ***: terminated\n"
}

public function __fdelt_chk {
  endbr64
  cmp rdi, 0x3ff
  ja fail
  mov eax, edi
  shr eax, 6
  ret
fail:
  lea rdi, __fdelt_chk_fail_msg
  jmp libc_fatal
}
