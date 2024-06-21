extern function pthread_kill_2_17 linkname("libpthread.so.0::pthread_kill@GLIBC_2.17");

public function pthread_kill_2_34 {
  // See https://github.com/bminor/glibc/commit/95dba35bf05e4a5d69dfae5e9c9d4df3646a7f93
  // and https://sourceware.org/bugzilla/show_bug.cgi?id=19193.
  // In some cases, pthread_kill@GLIBC_2.17 returns ESRCH (3), whereas pthread_kill@GLIBC_2.34
  // rerurns 0 for those cases. We call pthread_kill@GLIBC_2.17 and then remap 3 to 0.
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x10]!
  mov.cfi fp, sp
  bl pthread_kill_2_17
  cmp w0, #3
  csel w0, wzr, w0, eq
  ldp.cfi fp, lr, [sp], #0x10
  autiasp.cfi
  ret.cfi
}

extern function __libc_start_main_2_17 linkname("libc.so.6::__libc_start_main@GLIBC_2.17");

public function __libc_start_main_2_34 {
  // See https://github.com/bminor/glibc/commit/035c012e32c11e84d64905efaf55e74f704d3668
  // and https://sourceware.org/bugzilla/show_bug.cgi?id=23323.
  // In versions of glibc prior to 2.33, __libc_csu_init was statically linked into the application
  // and had its address passed in rcx. Since 2.34, NULL is passed instead, and __libc_start_main@GLIBC_2.34
  // knows to treat NULL specially. We do the converse: replace NULL with a pointer to statically linked code.
  bti c
  cmp x3, #0
  adr x9, call_exec_init_functions
  csel x3, x9, x3, eq
  b __libc_start_main_2_17

call_exec_init_functions:
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x40]!
  mov.cfi fp, sp
  stp.cfi x20, x21, [sp, #0x10]
  stp.cfi x22, x23, [sp, #0x20]
  stp.cfi x24, x25, [sp, #0x30]
  sub.cfi sp, sp, #224
  mov w25, #0

  // Save argc, argv, envp for later.
  mov w20, w0
  mov x21, x1
  mov x22, x2

  // Skip over argv.
  add x1, x1, #8
  add x1, x1, w0 uxtw #3

  // Skip over envp.
envp_loop:
  ldr x0, [x1], #8
  cbnz x0, envp_loop

  // Pull AT_PHDR (3) and AT_PHNUM (5) out of the aux vector.
  str xzr, [sp + #24]
  str xzr, [sp + #40]
auxv_loop:
  ldp x0, x2, [x1], #16
  cmp x0, #5
  b.hi auxv_loop
  str x2, [sp + x0 lsl #3]
  cbnz w0, auxv_loop
  ldr x1, [sp + #24] // AT_PHDR
  ldr x2, [sp + #40] // AT_PHNUM
  mov x4, x1         // Save copy of AT_PHDR
  lsl x3, x2, #6
  sub x2, x3, x2 lsl #3
  add x2, x2, x1     // Now AT_PHDR + AT_PHNUM * sizeof(Elf64_Ehdr).

  // Pull PT_PHDR (6) and PT_DYNAMIC (2) out of the program headers.
  str xzr, [sp + #16]
  str xzr, [sp + #48]
phdr_loop:
  cmp x1, x2
  b.hs done_phdr_loop
phdr_loop_neck:
  ldr w0, [x1], #56
  cmp w0, #6
  b.hi phdr_loop
  ldr x3, [x1, #-40]
  str x3, [sp + x0 lsl #3]
  cmp x1, x2
  b.lo phdr_loop_neck
done_phdr_loop:
  ldr x1, [sp + #16] // PT_DYNAMIC
  ldr x5, [sp + #48] // PT_PHDR
  sub x4, x4, x5     // Now load bias.
  add x1, x1, x4     // Apply load bias to PT_DYNAMIC.

  // Pull DT_INIT (12), DT_INIT_ARRAY (25), DT_INIT_ARRAYSZ (27) out of the dynamic array.
  str xzr, [sp + #96]
  str xzr, [sp + #200]
  str xzr, [sp + #216]
dt_loop:
  ldp x0, x2, [x1], #16
  cmp x0, #27
  b.hi dt_loop
  str x2, [sp + x0 lsl #3]
  cbnz x0, dt_loop
  ldr x3, [sp + #96]   // DT_INIT
  ldr x23, [sp + #200] // DT_INIT_ARRAY
  ldr x24, [sp + #216] // DT_INIT_ARRAYSZ
  add.cfi sp, sp, #224   // No longer need this memory.
  cmp x23, #0
  csel x24, xzr, x24, eq // Replace DT_INIT_ARRAYSZ with 0 if DT_INIT_ARRAY was NULL.
  add x23, x23, x4       // Add load bias to DT_INIT_ARRAY.
  and x24, x24, #-8      // DT_INIT_ARRAYSZ is in bytes; replace with complete qword count.
  add x24, x24, x23      // Now pointer to end of DT_INIT_ARRAY.

  // Call DT_INIT, if present.
  cbz x3, done_dt_init
  add x3, x3, x4 // Add load bias to DT_INIT.
  mov w0, w20
  mov x1, x21
  mov x2, x22
  cbnz x25, tripwire // Make life harder for gadgets.
  blr x3
done_dt_init:

  // Call DT_INIT_ARRAY contents.
  cmp x23, x24
  b.hs done_dt_init_array
dt_init_array_loop:
  ldr x3, [x23], #8
  mov w0, w20
  mov x1, x21
  mov x2, x22
  cbnz x25, tripwire // Make life harder for gadgets.
  blr x3
  cmp x23, x24
  b.lo dt_init_array_loop
done_dt_init_array:

  // Cleanup.
  ldp.cfi x24, x25, [sp, #0x30]
  ldp.cfi x22, x23, [sp, #0x20]
  ldp.cfi x20, x21, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x40
  autiasp.cfi
  ret.cfi

tripwire:
  udf
}

extern function clock_getres linkname("libc.so.6::clock_getres@GLIBC_2.17");

public function timespec_getres {
  paciasp.cfi
  cmp w1, #1
  mov x1, x0
  mov x0, #0
  b.ne unsupported_base
  stp.cfi fp, lr, [sp, #-0x10]!
  bl clock_getres
  ldp.cfi fp, lr, [sp], #0x10
  mov x0, #1
unsupported_base:
  autiasp.cfi
  ret.cfi
}
