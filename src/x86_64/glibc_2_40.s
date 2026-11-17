extern function inet_ntop_2_2_5 linkname("libc.so.6::inet_ntop@GLIBC_2.2.5");
extern function inet_pton_2_2_5 linkname("libc.so.6::inet_pton@GLIBC_2.2.5");
extern function free_2_2_5 linkname("libc.so.6::free@GLIBC_2.2.5");

public function sched_getattr {
  endbr64
  mov r10d, ecx
  mov eax, 315
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function sched_setattr {
  endbr64
  mov r10d, edx
  mov eax, 314
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function __inet_ntop_chk {
  endbr64
  mov r9d, ecx
  cmp r9, r8
  ja __chk_fail
  jmp inet_ntop_2_2_5
}

public function __inet_pton_chk {
  endbr64
  cmp edi, 2
  jne check_inet6_size
  cmp rcx, 4
  jb __chk_fail
  jmp inet_pton_2_2_5
check_inet6_size:
  cmp edi, 10
  jne call_inet_pton
  cmp rcx, 16
  jb __chk_fail
call_inet_pton:
  jmp inet_pton_2_2_5
}

public function uabs {
  endbr64
  mov eax, edi
  test edi, edi
  jns done
  neg eax
done:
  ret
}

public function ulabs {
  endbr64
  mov rax, rdi
  test rdi, rdi
  jns done
  neg rax
done:
  ret
}

public function ullabs {
  endbr64
  mov rax, rdi
  test rdi, rdi
  jns done
  neg rax
done:
  ret
}

public function uimaxabs {
  endbr64
  mov rax, rdi
  test rdi, rdi
  jns done
  neg rax
done:
  ret
}

public function memalignment {
  endbr64
  mov rax, rdi
  neg rax
  and rax, rdi
  ret
}

public function free_sized {
  endbr64
  jmp free_2_2_5
}

public function free_aligned_sized {
  endbr64
  jmp free_2_2_5
}

public function memset_explicit {
  endbr64
  mov r8, rdi
  mov eax, esi
  mov rcx, rdx
  rep stosb
  mov rax, r8
  ret
}

public function __memset_explicit_chk {
  endbr64
  cmp rdx, rcx
  ja __chk_fail
  jmp memset_explicit
}

public function mseal {
  endbr64
  mov eax, 462
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function openat2 {
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push 2
  push rdi
  push rdi
  push rsi
  push rdx
  push rcx
  mov edi, 1
  lea rsi, [rbp - 8]
  call pthread_setcanceltype
  pop r10
  pop rdx
  pop rsi
  pop rdi
  mov eax, 437
  syscall
  mov [rsp], rax
  lea rsi, [rbp - 8]
  mov edi, dword ptr [rsi]
  call pthread_setcanceltype
  pop rax
  leave.cfi
  cmp rax, -0xfff
  jae syscall_errno
  ret
}
