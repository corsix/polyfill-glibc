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
  bti c
  adr x0, _polyfill_DT_RELR_data
  ldp x1, x2, [x0]
  subs x4, x0, x1 // x4 is now amount to relocate by
  b.eq done
  ldr x3, [x0, #16]
  add x3, x3, x0 // x3 is now DT_RELR payload
  add x2, x2, x3 // x2 is now end pointer
  mov x0, #0
  mov x8, #1 lsl #63
outer_loop:
  ldr x5, [x3], #8
  rbit x6, x5
  adds x6, x6, x6 // Shift out high bit of x6 (low bit of x5) into CF
  b.cs is_bitset
  // Low bit was 0, so x5 is the address at which to perform one relocation.
  add x0, x4, x5
  ldr x6, [x0]
  add x6, x6, x4
  str x6, [x0], #8
  b outer_tail
is_bitset:
  // Low bit was 1, so remaining 63 bits are a mask of where to apply relocations.
  b.eq done_bitset
bitset_loop:
  clz x5, x6
  lsr x7, x8, x5
  bics x6, x6, x7
  ldr x7, [x0, x5 lsl #3]
  add x7, x7, x4
  str x7, [x0, x5 lsl #3]
  b.ne bitset_loop
done_bitset:
  add x0, x0, #504 // Advance pointer by 63 qwords, in case next outer iteration is another bitset.
outer_tail:
  cmp x3, x2
  b.lo outer_loop
done:
  mov x0, #0
  ret.cfi
}

const variable _polyfill_entry_original {
  qword 0 // Actually populated by polyfill.c
}

function _polyfill_entry {
  // Wrapper around the ELF entry point that calls _polyfill_init first.
  // Required for hooking into the init chain when polyfilling an executable
  // whose entry point calls __libc_start_main@GLIBC_2.17.
  bti c
  mov fp, #0
  mov x20, sp
  mov x21, x0
  ldr w0, [x20] // argc
  add x1, sp, #8 // argv
  add x2, sp, #16
  add x2, x2, x0 lsl #3 // envp (immediately after argv)
  and sp, x20, #-16
  bl _polyfill_init
  mov x0, x21
  mov sp, x20
  adr x17, _polyfill_entry_original
  ldr x16, [x17]
  add x16, x16, x17
  br x16
}

const variable _polyfill_DT_INIT_original {
  qword 0 // Actually populated by polyfill.c
}

function _polyfill_DT_INIT {
  // Wrapper around a DT_INIT function that calls _polyfill_init first.
  // Required for hooking into the init chain when polyfilling an executable
  // whose entry point calls __libc_start_main@GLIBC_2.34, or when polyfilling
  // a shared library.
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov.cfi fp, sp
  bl _polyfill_init
  adr x17, _polyfill_DT_INIT_original
  ldr x16, [x17]
  add x16, x16, x17
  ldp.cfi fp, lr, [sp], #0x10
  autiasp.cfi
  br x16
}

const variable _polyfill_init_array {
  // Actually populated by polyfill.c
}

const variable _polyfill_init_array_count {
  dword 0 // Actually populated by polyfill.c
}

variable _polyfill_init_done {
  dword 0
}

function _polyfill_init {
  // Has signature (int argc, char **argv, char **envp).
  // Calls a list of relative function pointers in _polyfill_init_array
  // having identical signature. To make life easier for _polyfill_DT_INIT,
  // _polyfill_init preserves x0, x1, x2.

  paciasp.cfi
  ldr w3, [_polyfill_init_done]
  cbnz w3, done

  stp.cfi fp, lr, [sp, #-0x30]!
  mov.cfi fp, sp
  stp.cfi x20, x21, [sp, #0x10]
  stp.cfi x22, x23, [sp, #0x20]

  // Save argc, argv, envp for later.
  mov x20, x0
  mov x21, x1
  mov x22, x2

  mov w23, #0
loop:
  adr x3, _polyfill_init_array
  ldrsw x16, [x3, x23 lsl #2]
  add x16, x16, x3
  ldr w3, [_polyfill_init_array_count]
  cmp x23, x3
  b.hs loop_done
  blr x16
  ldr w3, [_polyfill_init_array_count]
  add w23, w23, #1
  mov x0, x20
  mov x1, x21
  mov x2, x22
  cmp x23, x3
  b.lo loop

loop_done:
  adr x3, _polyfill_init_done
  mov w16, #1
  str w16, [x3]
  ldp.cfi x20, x21, [sp, #0x10]
  ldp.cfi x22, x23, [sp, #0x20]
  ldp.cfi fp, lr, [sp], #0x30
done:
  autiasp.cfi
  ret.cfi
}

variable _polyfill_cfi_object {
  qword 0, 0, 0, 0, 0, 0, 0, 0
}

const variable _polyfill_cfi_data {
  // Actually populated by polyfill.c
}

extern function __register_frame_info linkname("libgcc_s.so.1::__register_frame_info");

init function _polyfill_init_cfi_strong {
  bti c
  adr x0, _polyfill_cfi_data
  adr x1, _polyfill_cfi_object
  b __register_frame_info
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

extern function dlsym linkname("libc.so.6::dlsym@GLIBC_2.34"); // Likely renamed to libdl.so.2::dlsym@GLIBC_2.17

init function _polyfill_init_cfi_weak {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov.cfi fp, sp

  mov x0, #0
  adr x1, str___deregister_frame_info
  bl dlsym
  cbz x0, try_dlopen
  adr x1, __deregister_frame_info_ptr
  str x0, [x1]
  mov x0, #0
  adr x1, str___register_frame_info
  bl dlsym
  adr x1, __register_frame_info_ptr
  str x0, [x1]
  cbnz x0, install_cfi
try_dlopen:
  mov x0, #0
  adr x1, str_dlopen
  bl dlsym
  cbz x0, fail
  mov x2, x0
  adr x0, str_libgcc_s_so_1
  mov x1, #0x101 // RTLD_GLOBAL | RTLD_LAZY
  blr x2
  cbz x0, fail
  mov x0, #0
  adr x1, str___deregister_frame_info
  bl dlsym
  cbz x0, fail
  adr x1, __deregister_frame_info_ptr
  str x0, [x1]
  mov x0, #0
  adr x1, str___register_frame_info
  bl dlsym
  adr x1, __register_frame_info_ptr
  str x0, [x1]
  cbz x0, fail
install_cfi:
  mov x16, x0
  adr x0, _polyfill_cfi_data
  adr x1, _polyfill_cfi_object
  cfi_remember_state
  ldp.cfi fp, lr, [sp], #0x10
  autiasp.cfi
  br x16
  phantom_ref _polyfill_fini_cfi_weak // For the matching __deregister_frame_info call.
fail:
  cfi_restore_state
  adr x1, __deregister_frame_info_ptr
  str x0, [x1]
  ldp.cfi fp, lr, [sp], #0x10
  autiasp.cfi
  ret.cfi
}

const variable _polyfill_fini_array {
  // Actually populated by polyfill.c
}

const variable _polyfill_fini_array_count {
  dword 0 // Actually populated by polyfill.c
}

function _polyfill_DT_FINI {
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x20]!
  mov.cfi fp, sp
  str.cfi x20, [sp, #0x10]

  mov w20, #0
loop:
  adr x1, _polyfill_fini_array
  ldr x0, [x1, x20 lsl #3]
  add x0, x0, x1
  ldr w1, [_polyfill_fini_array_count]
  cmp x20, x1
  b.hs loop_done
  blr x0
  ldr w1, [_polyfill_fini_array_count]
  add w20, w20, #1
  cmp w20, w1
  b.lo loop

loop_done:
  ldr.cfi x20, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x20
  autiasp.cfi
  ret.cfi
}

fini function _polyfill_fini_cfi_weak {
  bti c
  ldr x16, [__deregister_frame_info_ptr]
  cbz x16, no_cfi
  adr x0, _polyfill_cfi_data
  br x16
no_cfi:
  ret.cfi
}

extern function __deregister_frame_info linkname("libgcc_s.so.1::__deregister_frame_info");

fini function _polyfill_fini_cfi_strong {
  bti c
  adr x0, _polyfill_cfi_data
  b __deregister_frame_info
}
