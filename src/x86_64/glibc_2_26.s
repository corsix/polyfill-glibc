public function __isnanf128 {
  endbr64
  movaps [rsp - 24], xmm0
  mov rax, [rsp - 16]
  add rax, rax
  rol rax, 15
  xor ecx, ecx
  sub ax, 0x7fff
  sete cl
  or rax, [rsp - 24]
  setne al
  and eax, ecx
  ret
}

extern function realloc linkname("libc.so.6::realloc@GLIBC_2.2.5");

public function reallocarray {
  endbr64
  mov rax, rsi
  mul rdx
  jo overflow
  mov rsi, rax
  jmp realloc
overflow:
  push.cfi rbp
  mov rbp, rsp
  call __errno_location
  mov dword ptr [rax], 12
  pop.cfi rbp
  xor eax, eax
  ret
}
