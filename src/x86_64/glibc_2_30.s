public function twalk_r {
  endbr64
  test rdi, rdi
  jz outer_tail // NULL tree?
  test rsi, rsi
  jz outer_tail // NULL callback function pointer?
  push.cfi rbp
  mov.cfi rbp, rsp
  push.cfi rbx
  push.cfi r12
  mov rbx, rsi // Save callback function pointer.
  mov r12, rdx // Save callback argument.
  call recurse
  pop.cfi r12
  pop.cfi rbx
  pop.cfi rbp
outer_tail:
  ret

recurse:
  mov rsi, qword ptr [rdi + 8]
  and rsi, -2
  mov rdx, r12 // For either leaf call or preorder call.
  or rsi, qword ptr [rdi + 16]
  jnz has_children // Has left or right node?
  // Leaf call.
  or esi, 3
  jmp rbx

has_children:
  // Preorder call.
  push.cfi r13
  mov r13, rdi
  xor esi, esi
  call rbx
  // Possible left recursion.
  mov rdi, qword ptr [r13 + 8]
  and rdi, -2
  jz done_left
  call recurse
done_left:
  // Postorder call.
  mov rdi, r13
  mov esi, 1
  mov rdx, r12
  call rbx
  // Possible right recursion.
  mov rdi, qword ptr [r13 + 16]
  test rdi, rdi
  jz done_right
  call recurse
done_right:
  // Endorder call.
  mov rdi, r13
  mov esi, 2
  mov rdx, r12
  pop.cfi r13
  jmp rbx
}

variable sem_clockwait_2_30_impl_kind {
  byte 0
}

variable sem_clockwait_2_30_impl_ptr {
  qword 0
}

const variable sem_clockwait_2_30_name {
  byte 0x30, 0
  asciiz "sem_clockwait"
}

const variable sem_clockwait_2_30_fudge_name {
  byte 2, 5, 0
  asciiz "sem_timedwait"
}

public function sem_clockwait_2_30 {
  endbr64
  test byte ptr [&sem_clockwait_2_30_impl_kind], -1
  mov rax, qword ptr [&sem_clockwait_2_30_impl_ptr]
  js jmp_rax
  jz fetch_impl
fudge_impl:
  cmp esi, 2
  jae syscall_EINVAL
  mov rsi, rdx
jmp_rax:
  jmp rax
fetch_impl:
  lea rax, sem_clockwait_2_30_name
  xor r10d, r10d
  call dlvsym_for_ac_syscall
  test rax, rax
  jz fetch_fudge_impl
  mov qword ptr [&sem_clockwait_2_30_impl_ptr], rax
  mov byte ptr [&sem_clockwait_2_30_impl_kind], -1
  jmp rax
fetch_fudge_impl:
  lea rax, sem_clockwait_2_30_fudge_name
  call dlvsym_for_ac_syscall
  mov qword ptr [&sem_clockwait_2_30_impl_ptr], rax
  mov byte ptr [&sem_clockwait_2_30_impl_kind], 1
  jmp fudge_impl
}
