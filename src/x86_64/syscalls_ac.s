// See ../../docs/Asynchronous_cancellation.md

extern function pthread_setcanceltype linkname("libc.so.6::pthread_setcanceltype@GLIBC_2.2.5");

variable accept4_2_10_impl_ptr {
  qword 0
}

const variable accept4_2_10_name {
  byte 0x10, 0
  asciiz "accept4"
}

public function accept4_2_10 {
  endbr64
  mov rax, qword ptr [&accept4_2_10_impl_ptr]
  test rax, rax
  jz determine_impl
  jmp rax
our_impl_ac:
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push 2
  push rdi // For alignment
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
  mov eax, 288
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
determine_impl:
  lea rax, accept4_2_10_name
  lea r10, our_impl_ac
  call dlvsym_for_ac_syscall
  mov qword ptr [&accept4_2_10_impl_ptr], rax
  jmp rax
}

variable preadv2_2_26_impl_ptr {
  qword 0
}

const variable preadv2_2_26_name {
  byte 0x26, 0
  asciiz "preadv2"
}

public function preadv2_2_26 {
  endbr64
  mov rax, qword ptr [&preadv2_2_26_impl_ptr]
  test rax, rax
  jz determine_impl
  jmp rax
our_impl_ac:
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push 2
  push rdi
  push rsi
  push rdx
  push rcx
  push r8
  mov edi, 1
  lea rsi, [rbp - 8]
  call pthread_setcanceltype
  pop r8
  pop r10
  pop rdx
  pop rsi
  pop rdi
  mov eax, 327
  syscall
  cmp rax, -38
  jnz done_emulate
  mov rax, -95
  test r8d, r8d
  jnz done_emulate
  cmp r10, -1
  mov ecx, 19
  mov eax, 295
  cmovz eax, ecx
  syscall
done_emulate:
  push rax
  lea rsi, [rbp - 8]
  mov edi, dword ptr [rsi]
  call pthread_setcanceltype
  pop rax
  leave.cfi
  cmp rax, -0xfff
  jae syscall_errno
  ret
determine_impl:
  lea rax, preadv2_2_26_name
  lea r10, our_impl_ac
  call dlvsym_for_ac_syscall
  mov qword ptr [&preadv2_2_26_impl_ptr], rax
  jmp rax
}

variable pwritev2_2_26_impl_ptr {
  qword 0
}

const variable pwritev2_2_26_name {
  byte 0x26, 0
  asciiz "pwritev2"
}

public function pwritev2_2_26 {
  endbr64
  mov rax, qword ptr [&pwritev2_2_26_impl_ptr]
  test rax, rax
  jz determine_impl
  jmp rax
our_impl_ac:
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push 2
  push rdi
  push rsi
  push rdx
  push rcx
  push r8
  mov edi, 1
  lea rsi, [rbp - 8]
  call pthread_setcanceltype
  pop r8
  pop r10
  pop rdx
  pop rsi
  pop rdi
  mov eax, 328
  syscall
  cmp rax, -38
  jnz done_emulate
  mov rax, -95
  test r8d, r8d
  jnz done_emulate
  cmp r10, -1
  mov ecx, 20
  mov eax, 296
  cmovz eax, ecx
  syscall
done_emulate:
  push rax
  lea rsi, [rbp - 8]
  mov edi, dword ptr [rsi]
  call pthread_setcanceltype
  pop rax
  leave.cfi
  cmp rax, -0xfff
  jae syscall_errno
  ret
determine_impl:
  lea rax, pwritev2_2_26_name
  lea r10, our_impl_ac
  call dlvsym_for_ac_syscall
  mov qword ptr [&pwritev2_2_26_impl_ptr], rax
  jmp rax
}

variable copy_file_range_2_27_impl_ptr {
  qword 0
}

const variable copy_file_range_2_27_name {
  byte 0x27, 0
  asciiz "copy_file_range"
}

public function copy_file_range_2_27 {
  endbr64
  mov rax, qword ptr [&copy_file_range_2_27_impl_ptr]
  test rax, rax
  jz determine_impl
  jmp rax
our_impl_ac:
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push 2
  push rax
  push rdi
  push rsi
  push rdx
  push rcx
  push r8
  push r9
  mov edi, 1
  lea rsi, [rbp - 8]
  call pthread_setcanceltype
  pop r9
  pop r8
  pop r10
  pop rdx
  pop rsi
  pop rdi
  mov eax, 326
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
determine_impl:
  lea rax, copy_file_range_2_27_name
  lea r10, our_impl_ac
  call dlvsym_for_ac_syscall
  mov qword ptr [&copy_file_range_2_27_impl_ptr], rax
  jmp rax
}

variable fcntl64_2_28_impl_ptr {
  qword 0
}

const variable fcntl64_2_28_name {
  byte 0x28, 0
  asciiz "fcntl64"
}

public function fcntl64_2_28 {
  endbr64
  mov rax, qword ptr [&fcntl64_2_28_impl_ptr]
  test rax, rax
  jz determine_impl
  jmp rax
our_impl:
  endbr64
  cmp esi, 7
  jz our_impl_ac
  cmp esi, 9
  jz our_impl_getown
  cmp esi, 38
  jz our_impl_ac
  // Not in any of the hard cases; just do a syscall.
  mov eax, 72
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
our_impl_getown:
  // F_GETOWN has an ambiguous return value, so we use F_GETOWN_EX instead.
  lea rdx, [rsp - 8]
  add esi, 7 // 9 + 7 == 16
  mov eax, 72
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  xor ecx, ecx
  cmp dword ptr [rsp - 8], 2
  mov eax, dword ptr [rsp - 4]
  setz cl
  neg ecx
  xor eax, ecx
  sub eax, ecx
  ret
our_impl_ac:
  // F_SETLKW / F_OFD_SETLKW can block, and are thus defined as cancellation points.
  push.cfi rbp
  mov.cfi rbp, rsp
  push rsi // Just for the stack adjustment
  push rdi
  push rsi
  push rdx
  mov edi, 1
  lea rsi, [rbp - 8]
  call pthread_setcanceltype
  pop rdx
  pop rsi
  pop rdi
  mov eax, 72
  syscall
  push rax
  lea rsi, [rbp - 8]
  mov edi, dword ptr [rsi]
  call pthread_setcanceltype
  pop rax
  leave.cfi
  cmp rax, -0xfff
  jae syscall_errno
  ret
determine_impl:
  lea rax, fcntl64_2_28_name
  lea r10, our_impl
  call dlvsym_for_ac_syscall
  mov qword ptr [&fcntl64_2_28_impl_ptr], rax
  jmp rax
}

variable epoll_pwait2_2_35_impl_ptr {
  qword 0
}

const variable epoll_pwait2_2_35_name {
  byte 0x35, 0
  asciiz "epoll_pwait2"
}

public function epoll_pwait2_2_35 {
  endbr64
  mov rax, qword ptr [&epoll_pwait2_2_35_impl_ptr]
  test rax, rax
  jz determine_impl
  jmp rax
our_impl_ac:
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push 2
  push rdi
  push rsi
  push rdx
  push rcx
  push r8
  mov edi, 1
  lea rsi, [rbp - 8]
  call pthread_setcanceltype
  pop r8
  pop r10
  pop rdx
  pop rsi
  pop rdi
  mov r9d, 8
  mov eax, 441
  syscall
  push rax
  lea rsi, [rbp - 8]
  mov edi, dword ptr [rsi]
  call pthread_setcanceltype
  pop rax
  leave.cfi
  cmp rax, -0xfff
  jae syscall_errno
  ret
determine_impl:
  lea rax, epoll_pwait2_2_35_name
  lea r10, our_impl_ac
  call dlvsym_for_ac_syscall
  mov qword ptr [&epoll_pwait2_2_35_impl_ptr], rax
  jmp rax
}

function dlvsym_for_ac_syscall {
  push.cfi rbp
  mov.cfi rbp, rsp
  push rdi
  push rsi
  push rdx
  push rcx
  push r8
  push r9
  push r10
  sub rsp, 16
  mov dword ptr [rsp], 'BILG'
  mov dword ptr [rsp + 4], '.2_C'
  mov rsi, rax
  lea rdi, [rsp + 7]
  lodsb
format_vstr:
  mov byte ptr [rdi], '.'
  add rdi, 1
  test al, 0xf0
  jz low_nibble
  mov ah, al
  shr al, 4
  and ah, 15
  add al, '0'
  stosb
  mov al, ah
low_nibble:
  add al, '0'
  stosb
  lodsb
  test al, al
  jnz format_vstr
  stosb
  xor edi, edi
  mov rdx, rsp
  call dlvsym
  add rsp, 16
  pop r10
  test rax, rax
  cmovz rax, r10
  pop r9
  pop r8
  pop rcx
  pop rdx
  pop rsi
  pop rdi
  pop.cfi rbp
  ret
}

extern function dlvsym linkname("libc.so.6::dlvsym@GLIBC_2.34"); // Likely renamed to libdl.so.2::dlvsym@GLIBC_2.2.5
