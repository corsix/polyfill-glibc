extern function pthread_kill_2_2_5 linkname("libpthread.so.0::pthread_kill@GLIBC_2.2.5");

public function pthread_kill_2_34 {
  // See https://github.com/bminor/glibc/commit/95dba35bf05e4a5d69dfae5e9c9d4df3646a7f93
  // and https://sourceware.org/bugzilla/show_bug.cgi?id=19193.
  // In some cases, pthread_kill@GLIBC_2.2.5 returns ESRCH (3), whereas pthread_kill@GLIBC_2.34
  // rerurns 0 for those cases. We call pthread_kill@GLIBC_2.2.5 and then remap 3 to 0.
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call pthread_kill_2_2_5
  xor ecx, ecx
  cmp eax, 3
  cmovz eax, ecx
  pop.cfi rbp
  ret
}

extern function __libc_start_main_2_2_5 linkname("libc.so.6::__libc_start_main@GLIBC_2.2.5");

public function __libc_start_main_2_34 {
  // See https://github.com/bminor/glibc/commit/035c012e32c11e84d64905efaf55e74f704d3668
  // and https://sourceware.org/bugzilla/show_bug.cgi?id=23323.
  // In versions of glibc prior to 2.33, __libc_csu_init was statically linked into the application
  // and had its address passed in rcx. Since 2.34, NULL is passed instead, and __libc_start_main@GLIBC_2.34
  // knows to treat NULL specially. We do the converse: replace NULL with a pointer to statically linked code.
  endbr64
  test rcx, rcx
  jnz tail
  lea rcx, call_exec_init_functions
tail:
  jmp __libc_start_main_2_2_5

call_exec_init_functions:
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push.cfi rbx
  push.cfi r12
  push.cfi r13
  push.cfi r14
  push.cfi r15
  sub rsp, 232

  // Save argc, argv, envp for later.
  mov ebx, edi
  mov r12, rsi
  mov r13, rdx

  // Skip over argv.
  lea rsi, [rsi + rdi*8 + 8]

  // Skip over envp.
  // TODO could be repne scasq
envp_loop:
  mov rax, qword ptr [rsi]
  add rsi, 8
  test rax, rax
  jnz envp_loop

  // Pull AT_PHDR (3) and AT_PHNUM (5) out of the aux vector.
  mov qword ptr [rsp + 24], rax
  mov qword ptr [rsp + 40], rax
auxv_loop:
  mov rax, qword ptr [rsi]
  add rsi, 16
  cmp rax, 5
  ja auxv_loop
  mov rcx, qword ptr [rsi - 8]
  mov qword ptr [rsp + rax * 8], rcx
  test rax, rax
  jnz auxv_loop
  mov rsi, qword ptr [rsp + 24] // AT_PHDR
  mov rdi, qword ptr [rsp + 40] // AT_PHNUM
  mov rdx, rsi                  // Save copy of AT_PHDR
  imul rdi, rdi, 56             // sizeof(Elf64_Ehdr)
  add rdi, rsi                  // Now AT_PHDR + AT_PHNUM * sizeof(Elf64_Ehdr).

  // Pull PT_PHDR (6) and PT_DYNAMIC (2) out of the program headers.
  mov qword ptr [rsp + 16], rax
  mov qword ptr [rsp + 48], rax
phdr_loop:
  cmp rsi, rdi
  jnb done_phdr_loop
phdr_loop_neck:
  mov eax, dword ptr [rsi]
  add rsi, 56
  cmp eax, 6
  ja phdr_loop
  mov rcx, qword ptr [rsi - 40]
  mov qword ptr [rsp + rax * 8], rcx
  cmp rsi, rdi
  jb phdr_loop_neck
done_phdr_loop:
  mov rsi, qword ptr [rsp + 16] // PT_DYNAMIC
  sub rdx, qword ptr [rsp + 48] // Use PT_PHDR to determine load bias.
  add rsi, rdx                  // Apply load bias to PT_DYNAMIC.

  // Pull DT_INIT (12), DT_INIT_ARRAY (25), DT_INIT_ARRAYSZ (27) out of the dynamic array.
  xor eax, eax
  mov qword ptr [rsp + 96], rax
  mov qword ptr [rsp + 200], rax
  mov qword ptr [rsp + 216], rax
dt_loop:
  mov rax, qword ptr [rsi]
  add rsi, 16
  cmp rax, 27
  ja dt_loop
  mov rcx, qword ptr [rsi - 8]
  mov qword ptr [rsp + rax * 8], rcx
  test rax, rax
  jnz dt_loop
  mov rax, qword ptr [rsp + 96]  // DT_INIT
  mov r14, qword ptr [rsp + 200] // DT_INIT_ARRAY
  mov r15, qword ptr [rsp + 216] // DT_INIT_ARRAYSZ
  add rsp, 232   // No longer need this memory.
  test r14, r14
  cmovz r15, r14 // Replace DT_INIT_ARRAYSZ with 0 if DT_INIT_ARRAY was NULL.
  add r14, rdx   // Add load bias to DT_INIT_ARRAY.
  and r15, -8    // DT_INIT_ARRAYSZ is in bytes; replace with complete qword count.
  add r15, r14   // Now pointer to end of DT_INIT_ARRAY.

  // Call DT_INIT, if present.
  test rax, rax
  jz done_dt_init
  add rax, rdx // Add load bias to DT_INIT.
  mov edi, ebx
  mov rsi, r12
  mov rdx, r13
  lea rcx, [rbp - 40]
  sub rcx, rsp
  jnz tripwire // Make life harder for gadgets.
  call rax
done_dt_init:

  // Call DT_INIT_ARRAY contents.
  cmp r14, r15
  jnb done_dt_init_array
dt_init_array_loop:
  mov rax, qword ptr [r14]
  add r14, 8
  mov edi, ebx
  mov rsi, r12
  mov rdx, r13
  lea rcx, [rbp - 40]
  sub rcx, rsp
  jnz tripwire // Make life harder for gadgets.
  call rax
  cmp r14, r15
  jb dt_init_array_loop
done_dt_init_array:

  // Cleanup.
  pop.cfi r15
  pop.cfi r14
  pop.cfi r13
  pop.cfi r12
  pop.cfi rbx
  cmp rbp, rsp
  jnz tripwire // Make life harder for gadgets.
  pop.cfi rbp
  ret

tripwire:
  int3
}

extern function clock_getres linkname("libc.so.6::clock_getres@GLIBC_2.17");

public function timespec_getres {
  endbr64
  cmp esi, 1
  jnz unsupported_base
  push.cfi rbp
  mov rbp, rsp
  mov rsi, rdi
  xor edi, edi
  call clock_getres
  mov eax, 1
  pop.cfi rbp
  ret
unsupported_base:
  xor eax, eax
  ret
}
