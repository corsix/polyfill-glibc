// This file contains functions and variables that polyfiller.c uses to handle
// various scenarios during process initialisation and teardown.

const variable _polyfill_DT_RELR_data {
  // Values actually populated by polyfill.c
  qword 0 // ideal virtual address of this variable
  qword 0 // DT_RELRSZ
  qword 0 // relative offset to DT_RELR payload
}

function _polyfill_DT_RELR_apply {
  // Called during relocation processing as an IRELATIVE resolver.
  // Parses DT_RELR and applies the relocations therein.
  endbr64
  lea rsi, _polyfill_DT_RELR_data
  mov rcx, rsi
  sub rcx, qword ptr [rsi] // rcx is now amount to relocate by
  jz done
  mov r8, qword ptr [rsi + 8] // DT_RELRSZ
  add rsi, qword ptr [rsi + 16] // DT_RELR payload
  add r8, rsi // r8 is now end pointer
  xor edx, edx
outer_loop:
  lodsq // rax = *rsi++
  shr rax, 1 // Shift out low bit into CF
  jc is_bitset
  // Low bit was 0, so pre-shifted rax was the address at which to perform one relocation.
  lea rdx, [rcx + rax*2 + 8]
  add qword ptr [rdx - 8], rcx // Apply one relocation
  jmp outer_tail
is_bitset:
  // Low bit was 1, so remaining 63 bits are a mask of where to apply relocations.
  bsf rdi, rax
  jz done_bitset
bitset_loop:
  btr rax, rdi
  add qword ptr [rdx + rdi * 8], rcx // Apply one relocation
  bsf rdi, rax
  jnz bitset_loop
done_bitset:
  add rdx, 504 // Advance pointer by 63 qwords, in case next outer iteration is another bitset.
outer_tail:
  cmp rsi, r8
  jb outer_loop
done:
  xor eax, eax
  ret
}

const variable _polyfill_entry_original {
  qword 0 // Actually populated by polyfill.c
}

function _polyfill_entry {
  // Wrapper around the ELF entry point that calls _polyfill_init first.
  // Required for hooking into the init chain when polyfilling an executable
  // whose entry point calls __libc_start_main@GLIBC_2.2.5.
  endbr64
  xor ebp, ebp
  mov edi, dword ptr [rsp] // argc
  lea rsi, [rsp + 8] // argv
  push r15
  push rdx
  lea rdx, [rsi + rdi*8 + 8] // envp (immediately after argv)
  mov r15, rsp
  and rsp, -16
  call _polyfill_init
  mov rsp, r15
  pop rdx
  pop r15
  lea rax, [&_polyfill_entry_original]
  add rax, qword ptr [rax]
  jmp rax
}

const variable _polyfill_DT_INIT_original {
  qword 0 // Actually populated by polyfill.c
}

function _polyfill_DT_INIT {
  // Wrapper around a DT_INIT function that calls _polyfill_init first.
  // Required for hooking into the init chain when polyfilling an executable
  // whose entry point calls __libc_start_main@GLIBC_2.34, or when polyfilling
  // a shared library.
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call _polyfill_init
  lea rax, [&_polyfill_DT_INIT_original]
  add rax, qword ptr [rax]
  pop.cfi rbp
  jmp rax
}

const variable _polyfill_init_array {
  // Actually populated by polyfill.c
}

variable _polyfill_init_done {
  byte 0
}

function _polyfill_init {
  // Has signature (int argc, char **argv, char **envp).
  // Calls a list of relative function pointers in _polyfill_init_array
  // having identical signature. To make life easier for _polyfill_DT_INIT,
  // _polyfill_init preserves edi, rsi, rdx.

  test byte ptr [&_polyfill_init_done], 1
  jnz done
  push.cfi rbp
  mov.cfi rbp, rsp
  push.cfi rbx
  push.cfi r12
  push.cfi r13
  push.cfi r14

  // Save argc, argv, envp for later.
  mov ebx, edi
  mov r12, rsi
  mov r13, rdx

  xor r14d, r14d
loop:
  lea rax, _polyfill_init_array
  movsx rcx, dword ptr [rax + r14 * 4 + 4]
  cmp r14d, dword ptr [rax]
  jae loop_done
  add rax, rcx
  call rax
  add r14d, 1
  mov edi, ebx
  mov rsi, r12
  mov rdx, r13
  cmp r14d, dword ptr [&_polyfill_init_array]
  jb loop

loop_done:
  pop.cfi r14
  pop.cfi r13
  pop.cfi r12
  pop.cfi rbx
  pop.cfi rbp
  mov byte ptr [&_polyfill_init_done], 1
done:
  ret
}

variable _polyfill_cfi_object {
  qword 0, 0, 0, 0, 0, 0, 0, 0
}

const variable _polyfill_cfi_data {
  // Actually populated by polyfill.c
}

extern function __register_frame_info linkname("libgcc_s.so.1::__register_frame_info");

init function _polyfill_init_cfi_strong {
  endbr64
  lea rdi, _polyfill_cfi_data
  lea rsi, _polyfill_cfi_object
  jmp __register_frame_info
  phantom_ref _polyfill_fini_cfi_strong // For the matching __deregister_frame_info call.
}

variable __register_frame_info_ptr {
  qword 0
}

variable __deregister_frame_info_ptr {
  qword 0
}

const variable str_libgcc_s_so_1 {
  asciiz "libgcc_s.so.1"
}

const variable str___register_frame_info {
  asciiz "__register_frame_info"
}

const variable str___deregister_frame_info {
  asciiz "__deregister_frame_info"
}

const variable str_dlopen {
  asciiz "dlopen"
}

extern function dlsym linkname("libc.so.6::dlsym@GLIBC_2.34"); // Likely renamed to libdl.so.2::dlsym@GLIBC_2.2.5

init function _polyfill_init_cfi_weak {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  xor edi, edi
  lea rsi, str___deregister_frame_info
  call dlsym
  test rax, rax
  jz try_dlopen
  mov qword ptr [&__deregister_frame_info_ptr], rax
  xor edi, edi
  lea rsi, str___register_frame_info
  call dlsym
  mov qword ptr [&__register_frame_info_ptr], rax
  test rax, rax
  jnz install_cfi
try_dlopen:
  xor edi, edi
  lea rsi, str_dlopen
  call dlsym
  test rax, rax
  jz fail
  lea rdi, str_libgcc_s_so_1
  mov esi, 0x101 // RTLD_GLOBAL | RTLD_LAZY
  call rax
  test rax, rax
  jz fail
  xor edi, edi
  lea rsi, str___deregister_frame_info
  call dlsym
  test rax, rax
  jz fail
  mov qword ptr [&__deregister_frame_info_ptr], rax
  xor edi, edi
  lea rsi, str___register_frame_info
  call dlsym
  mov qword ptr [&__register_frame_info_ptr], rax
  test rax, rax
  jz fail
install_cfi:
  lea rdi, _polyfill_cfi_data
  lea rsi, _polyfill_cfi_object
  cfi_remember_state
  pop.cfi rbp
  jmp rax
  phantom_ref _polyfill_fini_cfi_weak // For the matching __deregister_frame_info call.
fail:
  cfi_restore_state
  mov qword ptr [&__deregister_frame_info_ptr], rax
  pop.cfi rbp
  ret
}

const variable _polyfill_fini_array {
  // Actually populated by polyfill.c
}

function _polyfill_DT_FINI {
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push.cfi rbx
  push.cfi rbx

  xor ebx, ebx
loop:
  lea rax, _polyfill_fini_array
  mov rcx, qword ptr [rax + rbx * 8 + 8]
  cmp rbx, qword ptr [rax]
  jae loop_done
  add rax, rcx
  call rax
  add rbx, 1
  cmp rbx, qword ptr [&_polyfill_fini_array]
  jb loop

loop_done:
  pop.cfi rbx
  leave.cfi
  ret
}

fini function _polyfill_fini_cfi_weak {
  endbr64
  mov rax, qword ptr [&__deregister_frame_info_ptr]
  test rax, rax
  jz no_cfi
  lea rdi, _polyfill_cfi_data
  jmp rax
no_cfi:
  ret
}

extern function __deregister_frame_info linkname("libgcc_s.so.1::__deregister_frame_info");

fini function _polyfill_fini_cfi_strong {
  endbr64
  lea rdi, _polyfill_cfi_data
  jmp __deregister_frame_info
}
