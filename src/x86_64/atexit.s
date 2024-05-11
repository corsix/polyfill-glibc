variable __cxa_thread_atexit_impl_2_18_impl_ptr {
  qword 0
}

variable __cxa_thread_atexit_impl_2_18_pthread_key {
  qword 0
}

const variable __cxa_thread_atexit_impl_2_18_name {
  byte 0x18, 0
  asciiz "__cxa_thread_atexit_impl"
}

const variable __cxa_thread_atexit_impl_2_18_fail_msg {
  asciiz "Fatal error: failed to register TLS destructor"
}

extern function need_nodelete impose_rename("", "+nodelete");

extern function pthread_getspecific linkname("libpthread.so.0::pthread_getspecific@GLIBC_2.2.5");
extern function pthread_key_delete linkname("libpthread.so.0::pthread_key_delete@GLIBC_2.2.5");
extern function __cxa_atexit linkname("libc.so.6::__cxa_atexit@GLIBC_2.2.5");

public function __cxa_thread_atexit_impl_2_18 {
  endbr64
  mov rax, qword ptr [&__cxa_thread_atexit_impl_2_18_impl_ptr]
  test rax, rax
  jz determine_impl
  jmp rax
determine_impl:
  lea rax, __cxa_thread_atexit_impl_2_18_name
  lea r10, our_impl
  call dlvsym_for_ac_syscall
  mov qword ptr [&__cxa_thread_atexit_impl_2_18_impl_ptr], rax
  jmp rax

  align 16
our_impl:
  phantom_ref need_nodelete
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push rdi
  push rsi
  mov rdi, qword ptr [&__cxa_thread_atexit_impl_2_18_pthread_key]
  test rdi, rdi
  jz need_key
  mov edi, edi
  call pthread_getspecific
  test rax, rax
  jz need_new_node
  // Node layout is:
  // struct node_t {
  //   uint32_t count; // offset 0
  //   uint32_t unused;
  //   node_t* next;   // offset 8
  //   void* fn[7];    // offset 16 + 8*i
  //   void* arg[7];   // offset 72 + 8*i
  // }
  mov ecx, [rax] // node_t::count
  cmp ecx, 7
  jae need_new_node
got_node:
  pop rsi
  pop rdi
  mov [rax + 72 + rcx * 8], rsi // node_t::arg[rcx]
  rol rdi, 17
  xor rdi, rax
  mov [rax + 16 + rcx * 8], rdi // node_t::fn[rcx]
  add ecx, 1
  mov [rax], ecx // node_t::count
  cfi_remember_state
  pop.cfi rbp
  xor eax, eax
some_ret_instruction:
  ret

  align 16
atexit_dtor:
  endbr64
  mov edi, [&__cxa_thread_atexit_impl_2_18_pthread_key]
  call pthread_getspecific
  test rax, rax
  jz some_ret_instruction
  mov rdi, rax
  align 16
pthread_key_dtor:
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push.cfi rbx
  push.cfi r12
  mov r12, rdi
  mov edi, [&__cxa_thread_atexit_impl_2_18_pthread_key]
  xor esi, esi
  call pthread_setspecific
cleanup_node:
  mov ebx, [r12] // node_t::count
  sub ebx, 1
  jb next_node
next_fn:
  mov rax, [r12 + 16 + rbx * 8] // node_t::fn[rbx]
  mov rdi, [r12 + 72 + rbx * 8] // node_t::arg[rcx]
  xor rax, r12
  ror rax, 17
  call rax
  sub ebx, 1
  jae next_fn
next_node:
  mov rdi, r12
  mov r12, [r12 + 8] // node_t::next
  call free
  test r12, r12
  jnz cleanup_node
  pop.cfi r12
  pop.cfi rbx
  pop.cfi rbp
  ret

  cfi_restore_state
need_key:
  sub rsp, 16
  mov rdi, rsp
  lea rsi, pthread_key_dtor
  call pthread_key_create
  test eax, eax
  jnz err
  mov edi, [rsp]
  add rsp, 16
  bts rdi, 32
  lock cmpxchg qword ptr [&__cxa_thread_atexit_impl_2_18_pthread_key], rdi
  jz cmpxchg_was_ok
  // Some other thread created the key; delete what we just created.
  mov edi, edi
  call pthread_key_delete
  jmp need_initial_node
cmpxchg_was_ok:
  // One thread also needs to register an atexit function; might as well be the
  // winner of the cmpxchg.
  lea rdi, atexit_dtor
  xor esi, esi
  xor edx, edx
  call __cxa_atexit
need_initial_node:
  xor eax, eax
need_new_node:
  push rax
  push rax
  mov edi, 128 // sizeof(node_t)
  call malloc
  test rax, rax
  jz err
  pop rcx
  push rax
  mov [rax + 8], rcx // node_t::next
  xor ecx, ecx
  mov [rax], ecx // node_t::count
  mov rsi, rax
  mov edi, dword ptr [&__cxa_thread_atexit_impl_2_18_pthread_key]
  call pthread_setspecific
  pop rax
  pop rdx // Garbage
  jmp got_node

err:
  lea rdi, __cxa_thread_atexit_impl_2_18_fail_msg
  call libc_fatal
  int3
}

const variable quick_exit_2_24_name {
  byte 0x24, 0
  asciiz "quick_exit"
}

extern function quick_exit_2_10_extern linkname("libc.so.6::quick_exit@GLIBC_2.10");

public function quick_exit_2_24 {
  endbr64
  lea rax, quick_exit_2_24_name
  mov r10, [&quick_exit_2_10_extern]
  call dlvsym_for_ac_syscall
  jmp rax
}

const variable quick_exit_2_10_name {
  byte 0x10, 0
  asciiz "quick_exit"
}

extern function _exit linkname("libc.so.6::_exit@GLIBC_2.2.5");

public function quick_exit_2_10 {
  endbr64
  lea rax, quick_exit_2_10_name
  mov r10, [&_exit]
  call dlvsym_for_ac_syscall
  jmp rax
  // NB: The fallback logic is meant to call any registered at_quick_exit
  // handlers, but we don't provide a polyfill for at_quick_exit at the
  // moment, so there are no such handlers. That makes _exit acceptable as a
  // fallback function.
}
