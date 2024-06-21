variable __cxa_thread_atexit_impl_2_18_impl_ptr {
  qword 0
}

variable __cxa_thread_atexit_impl_2_18_pthread_key {
  qword 0
}

const variable __cxa_thread_atexit_impl_2_18_descriptor {
  relative_offset_i32 __cxa_thread_atexit_impl_2_18_impl_ptr
  byte '1', '8'
  asciiz "__cxa_thread_atexit_impl"
}

const variable __cxa_thread_atexit_impl_2_18_fail_msg {
  asciiz "Fatal error: failed to register TLS destructor"
}

extern function need_nodelete impose_rename("", "+nodelete");

extern function pthread_getspecific linkname("libpthread.so.0::pthread_getspecific@GLIBC_2.17");
extern function pthread_key_delete linkname("libpthread.so.0::pthread_key_delete@GLIBC_2.17");
extern function __cxa_atexit linkname("libc.so.6::__cxa_atexit@GLIBC_2.17");
extern function malloc linkname("libc.so.6::malloc@GLIBC_2.17");
extern function free linkname("libc.so.6::free@GLIBC_2.17");

public function __cxa_thread_atexit_impl_2_18 {
  bti c
  ldr x17, [__cxa_thread_atexit_impl_2_18_impl_ptr]
  cbz x17, determine_impl
  br x17
determine_impl:
  adr x16, __cxa_thread_atexit_impl_2_18_descriptor
  adr x17, our_impl
  b dlvsym_for_ac_syscall

align 16
our_impl:
  phantom_ref need_nodelete
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x30]!
  mov fp, sp
  stp.cfi x20, x21, [sp, #0x10]
  str.cfi x22, [sp, #0x20]
  mov x20, x0
  mov x21, x1
  adr x2, __cxa_thread_atexit_impl_2_18_pthread_key
  ldar x2, [x2]
  cbz x2, need_key
  mov w0, w2
  bl pthread_getspecific
  cbz x0, need_new_node
  // Node layout is:
  // struct node_t {
  //   uint32_t count; // offset 0
  //   uint32_t zero;
  //   node_t* next;   // offset 8
  //   struct {
  //     void* fn;    // offset 16 + 16*i
  //     void* arg;   // offset 24 + 16*i
  //   } entry[7];
  // }
  ldr w3, [x0] // node_t::count
  cmp w3, #7
  b.hs need_new_node
got_node:
  add x2, x0, x3 lsl #4
  pacib x20, x0
  stp x20, x21, [x2, #16] // node_t::fn[x3] and node_t::arg[x3]
  add w3, w3, #1
  str w3, [x0] // node_t::count
  cfi_remember_state
  ldr.cfi x22, [sp, #0x20]
  ldp.cfi x20, x21, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x30
  autiasp.cfi
some_ret_instruction:
  ret.cfi

atexit_dtor:
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov fp, sp
  adr x0, __cxa_thread_atexit_impl_2_18_pthread_key
  ldar w0, [x0]
  bl pthread_getspecific
  ldp.cfi fp, lr, [sp], #0x10
  autiasp.cfi
  cbz x0, some_ret_instruction
pthread_key_dtor:
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x20]!
  mov fp, sp
  stp.cfi x20, x21, [sp, #0x10]
  mov x20, x0
  adr x0, __cxa_thread_atexit_impl_2_18_pthread_key
  ldar w0, [x0]
  mov x1, #0
  bl pthread_setspecific
cleanup_node:
  ldr w21, [x20] // node_t::count
  subs w21, w21, #1
  b.lt next_node
next_fn:
  add x0, x20, x21 lsl #4
  ldp x1, x0, [x0, #16] // node_t::fn[x21] and node_t::arg[x21]
  autib x1, x20
  blr x1
  subs w21, w21, #1
  b.ge next_fn
next_node:
  mov x0, x20
  ldr x20, [x20, #8] // node_t::next
  bl free
  cbnz x20, cleanup_node
  ldp.cfi x20, x21, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x20
  autiasp.cfi
  ret.cfi;

  cfi_restore_state
need_key:
  add x0, sp, #0x28
  adr x1, pthread_key_dtor
  bl pthread_key_create
  cbnz w0, err
  ldr w0, [sp, #0x28]
  orr x0, x0, #1 lsl #32
  adr x1, __cxa_thread_atexit_impl_2_18_pthread_key
cmpxchg_loop:
  ldaxr x2, [x1]
  cbnz x2, cmpxchg_fail
  stxr w2, x0, [x1]
  cbnz w2, cmpxchg_loop
  // One thread also needs to register an atexit function; might as well be the
  // winner of the cmpxchg.
  adr x0, atexit_dtor
  mov x1, #0
  mov x2, #0
  bl __cxa_atexit
  b need_initial_node
cmpxchg_fail:
  // Some other thread created the key; delete what we just created.
  clrex
  mov w0, w0
  bl pthread_key_delete
need_initial_node:
  mov x0, #0
need_new_node:
  mov x22, x0
  mov x0, #128 // sizeof(node_t)
  bl malloc
  cbz x0, err
  stp xzr, x22, [x0] // node_t::count, node_t::next
  mov x22, x0
  mov x1, x0
  adr x0, __cxa_thread_atexit_impl_2_18_pthread_key
  ldar w0, [x0]
  bl pthread_setspecific
  mov x0, x22
  mov x3, #0
  b got_node

err:
  adr x0, __cxa_thread_atexit_impl_2_18_fail_msg
  bl libc_fatal
  udf
}

const variable quick_exit_2_24_descriptor {
  dword 0
  byte '2', '4'
  asciiz "quick_exit"
}

extern function quick_exit_2_17_extern linkname("libc.so.6::quick_exit@GLIBC_2.17");

public function quick_exit_2_24 {
  bti c
  adr x16, quick_exit_2_24_descriptor
  adr x17, quick_exit_2_17_extern
  b dlvsym_for_ac_syscall
}
