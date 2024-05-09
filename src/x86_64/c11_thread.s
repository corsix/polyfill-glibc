// C11 <threads.h> functions.

extern function pthread_cond_broadcast linkname("libpthread.so.0::pthread_cond_broadcast@GLIBC_2.3.2");

public function cnd_broadcast {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call pthread_cond_broadcast
  jmp thrd_err_remap
}

extern function pthread_cond_init linkname("libpthread.so.0::pthread_cond_init@GLIBC_2.3.2");

public function cnd_init {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  xor esi, esi
  call pthread_cond_init
  jmp thrd_err_remap
}

extern function pthread_cond_signal linkname("libpthread.so.0::pthread_cond_signal@GLIBC_2.3.2");

public function cnd_signal {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call pthread_cond_signal
  jmp thrd_err_remap
}

extern function pthread_cond_timedwait linkname("libpthread.so.0::pthread_cond_timedwait@GLIBC_2.3.2");

public function cnd_timedwait {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call pthread_cond_timedwait
  jmp thrd_err_remap
}

extern function pthread_cond_wait linkname("libpthread.so.0::pthread_cond_wait@GLIBC_2.3.2");

public function cnd_wait {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call pthread_cond_wait
  jmp thrd_err_remap
}

extern function pthread_mutexattr_init linkname("libpthread.so.0::pthread_mutexattr_init@GLIBC_2.2.5");
extern function pthread_mutexattr_settype linkname("libpthread.so.0::pthread_mutexattr_settype@GLIBC_2.2.5");
extern function pthread_mutex_init linkname("libpthread.so.0::pthread_mutex_init@GLIBC_2.2.5");

public function mtx_init {
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push rdi
  push rsi // 32 bits of type, other 32 bits used for attrs
  // Initialise a pthread_mutexattr_t.
  lea rdi, [rbp - 12]
  call pthread_mutexattr_init
  // Remap 1,3 -> 1; anything else -> 0, passing this to pthread_mutexattr_settype.
  lea rdi, [rbp - 12]
  xor esi, esi
  mov eax, dword ptr [rsp]
  and eax, -3
  cmp eax, 1
  setz sil
  call pthread_mutexattr_settype
  // Now call pthread_mutex_init with our pthread_mutexattr_t.
  mov rdi, qword ptr [rbp - 8]
  lea rsi, [rbp - 12]
  call pthread_mutex_init
  // Usual tail.
  mov rsp, rbp
  jmp thrd_err_remap
}

extern function pthread_mutex_lock linkname("libpthread.so.0::pthread_mutex_lock@GLIBC_2.2.5");

public function mtx_lock {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call pthread_mutex_lock
  jmp thrd_err_remap
}

extern function pthread_mutex_timedlock linkname("libpthread.so.0::pthread_mutex_timedlock@GLIBC_2.2.5");

public function mtx_timedlock {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call pthread_mutex_timedlock
  jmp thrd_err_remap
}

extern function pthread_mutex_trylock linkname("libpthread.so.0::pthread_mutex_trylock@GLIBC_2.2.5");

public function mtx_trylock {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call pthread_mutex_trylock
  jmp thrd_err_remap
}

extern function pthread_mutex_unlock linkname("libpthread.so.0::pthread_mutex_unlock@GLIBC_2.2.5");

public function mtx_unlock {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call pthread_mutex_unlock
  jmp thrd_err_remap
}

extern function pthread_create linkname("libpthread.so.0::pthread_create@GLIBC_2.2.5");

public function thrd_create {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  mov rcx, rdx
  mov rdx, rsi
  or rsi, -1
  call pthread_create
  jmp thrd_err_remap
}

extern function pthread_detach linkname("libpthread.so.0::pthread_detach@GLIBC_2.2.5");

public function thrd_detach {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call pthread_detach
  jmp thrd_err_remap
}

extern function pthread_exit linkname("libpthread.so.0::pthread_exit@GLIBC_2.2.5");

public function thrd_exit {
  endbr64
  movsx rdi, edi
  jmp pthread_exit
}

extern function pthread_join linkname("libpthread.so.0::pthread_join@GLIBC_2.2.5");

public function thrd_join {
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push rsi
  push rsi
  mov rsi, rsp
  call pthread_join
  pop rdi
  pop rsi
  test rsi, rsi
  jz thrd_err_remap
  mov dword ptr [rsi], edi
  jmp thrd_err_remap
}

extern function clock_nanosleep linkname("libc.so.6::clock_nanosleep@GLIBC_2.17");

public function thrd_sleep {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  mov rdx, rdi
  xor edi, edi
  mov rcx, rsi
  xor esi, esi
  call clock_nanosleep
  test eax, eax
  jz tail
  cmp eax, 4
  sete cl
  movzx eax, cl
  sub eax, 2
tail:
  pop.cfi rbp
  ret
}

public function thrd_yield {
  endbr64
  mov eax, 24
  syscall
  ret
}

extern function pthread_key_create linkname("libpthread.so.0::pthread_key_create@GLIBC_2.2.5");

public function tss_create {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call pthread_key_create
  jmp thrd_err_remap
}

extern function pthread_setspecific linkname("libpthread.so.0::pthread_setspecific@GLIBC_2.2.5");

public function tss_set {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call pthread_setspecific
  jmp thrd_err_remap
}

function thrd_err_remap {
  // 0 -> thrd_success (0)
  // ENOMEM (12) -> thrd_nomem (3)
  // ETIMEDOUT (110) -> thrd_timedout (4)
  // EBUSY (16) -> thrd_busy (1)
  // default -> thrd_error (2)
  cfi_byte 0x0c, 7, 16, 0x86, 2 // DW_CFA_def_cfa(RSP, +16), DW_CFA_offset(RBP, -2)
  pop rbp
  cfi_byte 0x0e, 8, 0xc6 // DW_CFA_def_cfa_offset(+8), DW_CFA_restore(RBP)
  cmp eax, 16
  ja large
  lea ecx, [eax*2 - 2]
  mov eax, 0x6aeaaaaa
  shr rax, cl
  and eax, 3
  ret
large:
  cmp eax, 110
  setz al
  movzx ecx, al
  lea eax, [ecx*2 + 2]
  ret
}
