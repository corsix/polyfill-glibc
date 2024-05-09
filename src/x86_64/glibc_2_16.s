extern function memalign linkname("libc.so.6::memalign@GLIBC_2.2.5");

public function aligned_alloc {
  endbr64
  mov rcx, rdi
  lea rax, [rdi - 1]
  xor rcx, rax
  cmp rax, rcx
  jnb err_EINVAL
  jmp memalign
err_EINVAL:
  push.cfi rbp
  mov rbp, rsp
  call __errno_location
  pop.cfi rbp
  mov dword ptr [rax], 22
  xor eax, eax
  ret
}

variable getauxval_base_ptr {
  qword 0
}

public function getauxval {
  endbr64
top:
  mov rax, qword ptr [&getauxval_base_ptr]
  and rax, -2
  phantom_ref getauxval_init // To initialise getauxval_base_ptr at startup.
  jz getauxval_init_has_not_run
loop:
  mov rcx, qword ptr [rax]
  add rax, 16
  test rcx, rcx
  jz not_found
  cmp rcx, rdi
  jnz loop
  mov rax, qword ptr [rax - 8]
  ret
not_found:
  push.cfi rbp
  mov rbp, rsp
  call __errno_location
  pop.cfi rbp
  mov dword ptr [rax], 2
  xor eax, eax
  ret

getauxval_init_has_not_run:
  // In almost every case, getauxval_init should initialise getauxval_base_ptr.
  // However, in rare cases, getauxval can get called before getauxval_init runs.
  // If we're in one of those cases, use mmap to allocate some memory, then
  // open /proc/self/auxv and read it into the newly allocated memory.
  mov qword ptr [rsp - 8], rdi // Save this for later.
  // First comes the mmap.
  xor edi, edi
  mov esi, 4096
  lea edx, [edi+3] // PF_R | PF_W
  lea r10d, [edi+0x22] // MAP_PRIVATE | MAP_ANONYMOUS
  lea r8d, [edi-1] // -1 (as fd)
  xor r9d, r9d
try_mmap:
  mov eax, 9 // __NR_mmap
  syscall
  cmp rax, -4 // EINTR
  jz try_mmap
  cmp rax, -0xfff
  jae not_found
  mov r8, rax // Save this for later.
  // Next up comes open of /proc/self/auxv.
  lea rdi, str_proc_self_auxv
  mov esi, 0x80100 // O_RDONLY | O_CLOEXEC | O_NOCTTY
try_open:
  mov eax, 2 // __NR_open
  syscall
  cmp rax, -4 // EINTR
  jz try_open
  cmp rax, -0xfff
  jae munmap_then_not_found
  // Then a read loop.
  mov edi, eax
  mov rsi, r8
  mov edx, 4080
  xor eax, eax
try_read:
  sub edx, eax
  jz read_complete
  add rsi, rax
try_read_no_add:
  xor eax, eax // __NR_read
  syscall
  test rax, rax
  jz read_complete
  jg try_read
  cmp eax, -4 // EINTR
  jz try_read_no_add
  // The read failed for some reason; close the fd, unmap the memory, then return empty.
  mov eax, 3 // __NR_close
  syscall
munmap_then_not_found:
  mov rdi, r8
  mov esi, 4096
try_munmap:
  mov eax, 11 // __NR_munmap
  syscall
  cmp rax, -4 // EINTR
  jz try_munmap
  jmp not_found

read_complete:
  // We have read everything from /proc/self/auxv, so close the fd.
  mov eax, 3 // __NR_close
  syscall
  // Try to store our memory block to getauxval_base_ptr.
  xor eax, eax
  lock cmpxchg qword ptr [&getauxval_base_ptr], r8
  phantom_ref getauxval_fini // To eventually do a matching munmap.
  mov rdi, qword ptr [rsp - 8]
  jz top
  // The cmpxchg failed, so free our memory and try again.
  mov rdi, r8
  mov esi, 4096
try_munmap_complete:
  mov eax, 11 // __NR_munmap
  syscall
  cmp rax, -4 // EINTR
  jz try_munmap_complete
  mov rdi, qword ptr [rsp - 8]
  jmp top
}

init function getauxval_init {
  // Called as DT_INIT, with argc in edi and argv in rsi.
  endbr64
  // Skip over argv.
  lea rdi, [rsi + rdi*8 + 8]
  // Skip over envp.
  xor eax, eax
  lea rcx, [rax - 1]
  repne scasq
  // What's left is auxv.
  or rdi, 1 // Marker bit to prevent getauxval_fini from freeing this pointer.
  lock cmpxchg qword ptr [&getauxval_base_ptr], rdi
  ret
}

fini function getauxval_fini {
  endbr64
  xor edi, edi
  xchg rdi, qword ptr [&getauxval_base_ptr]
  test rdi, rdi
  jz done // rdi == NULL? Could happen if getauxval_init never ran.
  test edi, 0xfff
  jnz done // Not the result of an mmap? Expected to happen when getauxval_init runs before the first getauxval call.
  mov esi, 4096
try_munmap:
  mov eax, 11 // __NR_munmap
  syscall
  cmp rax, -4 // EINTR
  jz try_munmap
done:
  ret
}

const variable str_proc_self_auxv {
  asciiz "/proc/self/auxv"
}
