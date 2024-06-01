function _polyfill_trampoline {
  endbr64
  push 0
  push 0
  mov rbp, rsp
  and rsp, -16
  lea rdi, [rbp + 16]
  lea rsi, _polyfill_our_data
  mov rbx, rdx
  call _polyfill_c_function
  mov rdx, rbx
  leave
  pop rbx
  jmp rax
}

function _polyfill_c_function {}
variable _polyfill_our_data {}
