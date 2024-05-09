public function posix_spawn {
  endbr64
  xor eax, eax
  jmp posix_spawn_internal
}

public function posix_spawnp {
  endbr64
  mov eax, 0x0100
  jmp posix_spawn_internal
}

public function pidfd_spawn {
  endbr64
  mov eax, 0x1000
  jmp posix_spawn_internal
}

public function pidfd_spawnp {
  endbr64
  mov eax, 0x1100
  jmp posix_spawn_internal
}

variable posix_spawn_internal_pidfd_checked {
  byte 0
}

function posix_spawn_internal {
  push.cfi rbp
  mov.cfi rbp, rsp
  // Save args to stack
  push rdi // pid_t* pid or int* pidfd
  push rsi // char* filename
  push rdx // posix_spawn_file_actions_t* file_actions
  push rcx // posix_spawnattr_t* attrp
  push r8 // char** argv
  push r9 // char** envp
  push rax // internal flags (only the ah byte is interesting, high 32 bits used for pidfd storage)
  lea rdx, [rbp - 52]

  // Transform internal flags to clone3 flags
  bts rax, 32 // CLONE_CLEAR_SIGHAND
  or rax, 0x4100 // CLONE_VM | CLONE_VFORK
  // Note that CLONE_VM and CLONE_VFORK are both specified, but on some systems
  // (such as WSL, or older versions of qemu-user) the flags are ignored. Hence
  // we cannot assume CLONE_VM or CLONE_VFORK semantics, but neither can we
  // assume their absence. Instead, a CLOEXEC pipe is used for communication and
  // synchronisation between parent and child.

  // clone3 args
  xor edi, edi
  test rcx, rcx
  jz not_setcgroup1 // attrp == NULL?
  test word ptr [rcx], 0x100 // POSIX_SPAWN_SETCGROUP
  jz not_setcgroup0 // POSIX_SPAWN_SETCGROUP not specified?
  movsx rdi, dword ptr [rcx + 0x110]
  bts rax, 33 // CLONE_INTO_CGROUP
not_setcgroup0:
  xor ecx, ecx
not_setcgroup1:
  push rdi // cgroup
  push rcx // set_tid_size
  push rcx // set_tid
  push rcx // tls
  push rcx // stack_size
  push rcx // stack
  push 17 // exit_signal (SIGCHLD)
  push rcx // parent_tid
  push rcx // child_tid
  test ah, 0x10
  cmovnz rcx, rdx
  push rcx // pidfd
  push rax // flags
  // Note that after the clone call has been performed, the stack region
  // containing the clone3 arg structure is re-used:
  //  - The parent splits the flags field into two 32-bit pieces, one
  //    piece for the child pid, one piece for the child exit value.
  //  - The child uses the pidfd field for pipe fd.
  //  - The child uses stack/exit_signal/parent_tid/child_tid as a sigaction
  //    output pointer.
  //  - The child uses set_tid_size/set_tid/tls/stack_size as a sigaction
  //    input pointer, relying on the zero values in these fields.
  //  - The child uses cgroup to store the final sigmask.

  // If requesting CLONE_PIDFD, check for support.
  jz checked_pidfd_support
  or al, byte ptr [&posix_spawn_internal_pidfd_checked]
  jnp checked_pidfd_support
  jnz no_pidfd_support
  xor edx, edx
  xor r8d, r8d
  mov esi, 0x7fffffff // INT32_MAX (an fd that is just plausible enough to get past waitid's EINVAL checks, but guaranteed to then EBADF)
  lea edi, [edx + 3] // P_PIDFD
  lea r10d, [edx + 5] // WEXITED | WNOHANG
  mov eax, 247 // __NR_waitid
  syscall
  cmp rax, -9 // EBADF
  setz al
  not al
  mov byte ptr [&posix_spawn_internal_pidfd_checked], al
  jnz no_pidfd_support
checked_pidfd_support:

  // Block all signals, saving old block mask.
  push -1 // Replaced by old mask, then read by both parent and child.
  xor edi, edi
  lea r10d, [edi + 8]
  mov rsi, rsp
  mov rdx, rsp
  mov eax, 14 // __NR_rt_sigprocmask
  syscall
  neg rax
  jnz no_pid_ret

  // Call pipe2 (or fallback to pipe + fcntl*2)
  push -1 // Replaced by pipe FDs, then read by both parent and child.
  mov rdi, rsp
  mov esi, 0x80000 // O_CLOEXEC
  mov eax, 293 // __NR_pipe2
  syscall
  cmp rax, -0xfff
  jb done_pipe
  cmp rax, -38 // ENOSYS
  jnz pipe_failed
  mov eax, 22 // __NR_pipe
  syscall
  neg rax
  jnz pipe_failed
  mov edi, [rsp + 4]
  mov edx, 1 // FD_CLOEXEC
  lea esi, [edx + 1] // F_SETFD
  mov eax, 72 // __NR_fcntl
  syscall
  cmp rax, -0xfff
  jae clone_failed
  mov edi, [rsp]
  mov eax, 72 // __NR_fcntl
  syscall
  cmp rax, -0xfff
  jae clone_failed
done_pipe:

  // Call clone3, fallback to clone
  lea rdi, [rsp + 16]
  mov esi, 0x58
  mov eax, 435
  syscall
  test rax, rax
  jz in_clone3_child
  jg done_clone
  cmp eax, -22 // EINVAL
  jz clone_fallback
  cmp eax, -38 // ENOSYS
  jnz clone_failed
clone_fallback:
  mov rdi, [rsp + 16]
  bt rdi, 33 // CLONE_INTO_CGROUP
  jc clone_could_not_fallback
  or edi, 17 // Set SIGCHLD, clear high bits.
  xor esi, esi
  mov rdx, [rsp + 24] // pidfd ptr
  xor r10d, r10d
  xor r8d, r8d
  mov eax, 56 // __NR_clone
  syscall
  test rax, rax
  jz in_clone_child
  jl clone_failed
done_clone:
  // NB: Might (or might not) be sharing memory with child.
  // Be careful until we have confirmed child exec or exit.
  // Need to save pid somewhere (overwrites low 32 bits of clone3 args flags)
  mov [rsp + 16], eax
  // Close write end of pipe
  mov edi, [rsp + 4]
  mov eax, 3 // __NR_close
  syscall
  // Read from pipe (overwrites high 32 bits of clone3 args flags)
  mov edi, [rsp]
  xor eax, eax
  lea rsi, [rsp + 20]
  lea edx, [eax + 4]
try_read:
  sub edx, eax
  jz child_failed
  add rsi, rax
try_read_no_add:
  xor eax, eax // __NR_read
  syscall
  test rax, rax
  jg try_read
  cmp eax, -4 // EINTR
  jz try_read_no_add
  // Read was incomplete (likely because child did an exec); treat as success.
  mov dword [rsp + 20], 0
  // Close read end of pipe
close_read_end:
  mov edi, [rsp]
  mov eax, 3 // __NR_close
  syscall
  // Restore signal mask
restore_signal_mask:
  xor edx, edx
  lea r10d, [edx + 8]
  lea edi, [edx + 2]
  lea rsi, [rsp + 8]
  mov eax, 14 // __NR_rt_sigprocmask
  syscall
  // Return
  mov eax, [rsp + 20]
  test eax, eax
  jnz no_pid_ret
  mov rcx, [rbp - 8]
  test rcx, rcx
  jz no_pid_ret
  test byte [rbp - 55], 0x10
  mov edx, [rsp + 16] // pid
  cmovnz edx, dword ptr [rbp - 52] // pidfd
  mov [rcx], edx
no_pid_ret:
  cfi_remember_state
  leave.cfi
  ret
  cfi_restore_state

no_pidfd_support:
  mov eax, 38 // ENOSYS
  jmp no_pid_ret

child_failed:
  // Clone allocated a pid, but we won't return it, so wait on it.
  mov edi, [rsp + 16]
  xor esi, esi
  xor edx, edx
  xor r10d, r10d
try_wait4:
  mov eax, 61 // __NR_wait4
  syscall
  cmp rax, -4 // EINTR
  jz try_wait4
  test byte [rbp - 55], 0x10
  jz close_read_end
  // Clone allocated a pidfd, but we won't return it, so close it.
  mov edi, dword ptr [rbp - 52]
  mov eax, 3 // __NR_close
  syscall
  jmp close_read_end

clone_could_not_fallback:
  cmp eax, -38 // ENOSYS
  jnz clone_failed
  sub eax, -57 // ENOSYS -> EOPNOTSUPP
clone_failed:
  // Store return value
  neg eax
  mov [rsp + 20], eax
  // Close write end of pipe
  mov edi, [rsp + 4]
  mov eax, 3 // __NR_close
  syscall
  jmp close_read_end
pipe_failed:
  // Store return value
  mov [rsp + 20], eax
  jmp restore_signal_mask

in_clone3_child:
  sub rax, 1
in_clone_child:
  // Determine the final sigmask
  mov r8, [rsp + 8] // Old block mask from parent blocking all signals.
  mov rcx, [rbp - 32] // posix_spawnattr_t* attrp
  test rcx, rcx
  jz no_setsigmask
  test byte ptr [rcx], 0x08
  jz no_setsigmask
  mov r8, [rcx + 0x88]
  btc r8, 31
  btc r8, 32
no_setsigmask:
  mov [rbp - 64], r8 // Store final sigmask for later
  or r8, rax // r8 is now the benign mask
  xor r9d, r9d
  test rcx, rcx
  jz no_setsigdef
  test byte ptr [rcx], 0x04
  jz no_setsigdef
  mov r9, [rcx + 0x08] // r9 is the POSIX_SPAWN_SETSIGDEF mask
no_setsigdef:

  // Set signal handlers to SIG_DFL / SIG_IGN as appropriate.
  xor edi, edi
  rol r8, 1 // Because we test this mask after incrementing rdi.
  lea r10d, [edi + 8]
sigaction_loop:
  bt r9, rdi
  lea edi, [edi + 1]
  jc set_SIG_DFL
  bt r8, rdi
  jc skip_this_sig
  lea eax, [edi - 32]
  sub eax, 2
  jb set_SIG_IGN // 32 <= edi <= 33? (internal signals used by glibc)
  xor esi, esi
  lea rdx, [rbp - 128]
  mov eax, 13 // __NR_rt_sigaction
  syscall
  cmp qword ptr [rdx], 2
  jb skip_this_sig // Disposition already SIG_DFL or SIG_IGN?
set_SIG_DFL:
  xor eax, eax
set_SIG_IGN: // When jumping to this label, high bit of eax must be set.
  lea rsi, [rbp - 96]
  shr eax, 31
  xor edx, edx
  mov [rsi], rax
  mov eax, 13 // __NR_rt_sigaction
  syscall
skip_this_sig:
  cmp edi, 64
  jb sigaction_loop

  // Do various attribute handling.
  mov r8, [rbp - 32] // posix_spawnattr_t* attrp
  test r8, r8
  jz done_all_attr_handling
  movzx r9d, word ptr [r8]
  test r9d, r9d
  jz done_all_attr_handling
  xor edi, edi
  test r9b, 0x20
  jz done_POSIX_SPAWN_SETSCHEDULER
  mov esi, dword ptr [r8 + 0x10c]
  lea rdx, [r8 + 0x108]
try_sched_setscheduler:
  mov eax, 144 // __NR_sched_setscheduler
  syscall
  cmp rax, -4 // EINTR
  jz try_sched_setscheduler
  cmp rax, -0xfff
  jae failed_syscall_in_child
  jmp done_POSIX_SPAWN_SETSCHEDPARAM
done_POSIX_SPAWN_SETSCHEDULER:
  test r9b, 0x10
  jz done_POSIX_SPAWN_SETSCHEDPARAM
  lea rsi, [r8 + 0x108]
try_sched_setparam:
  mov eax, 142 // __NR_sched_setparam
  syscall
  cmp rax, -4 // EINTR
  jz try_sched_setparam
  cmp rax, -0xfff
  jae failed_syscall_in_child
done_POSIX_SPAWN_SETSCHEDPARAM:
  test r9b, 0x80
  jz done_POSIX_SPAWN_SETSID
try_setsid:
  mov eax, 112 // __NR_setsid
  syscall
  cmp rax, -4 // EINTR
  jz try_setsid
  cmp rax, -0xfff
  jae failed_syscall_in_child
done_POSIX_SPAWN_SETSID:
  test r9b, 0x02
  jz done_POSIX_SPAWN_SETPGROUP
  lea esi, dword ptr [r8 + 0x4]
try_setpgid:
  mov eax, 109 // __NR_setpgid
  syscall
  cmp rax, -4 // EINTR
  jz try_setpgid
  cmp rax, -0xfff
  jae failed_syscall_in_child
done_POSIX_SPAWN_SETPGROUP:
  test r9b, 0x01
  jz done_all_attr_handling
  sub edi, 1
  mov edx, edi
  mov eax, 104 // __NR_getgid
  syscall
  mov rsi, rax
  mov eax, 119 // __NR_setresgid
  syscall
  cmp rax, -0xfff
  jae failed_syscall_in_child
  mov eax, 102 // __NR_getuid
  syscall
  mov rsi, rax
  mov eax, 117 // __NR_setresuid
  syscall
  cmp rax, -0xfff
  jae failed_syscall_in_child
done_all_attr_handling:

  // Store write end of pipe somewhere, in case we need to renumber it.
  mov edi, [rsp + 4]
  mov [rsp + 24], edi

  // Next up is file action handling.
  mov rdx, [rbp - 24] // posix_spawn_file_actions_t* file_actions
  test rdx, rdx
  jz done_all_file_actions
  mov ecx, [rdx + 4]
  test ecx, ecx
  jz done_all_file_actions
  shl ecx, 5
  mov r8, [rdx + 8]
  lea r9, [r8 + rcx]
file_action_loop:
  mov ecx, [r8]
  cmp ecx, 6
  ja unknown_file_action
  u8_label_lut(rax, rcx, spawn_do_close, spawn_do_dup2, spawn_do_open, spawn_do_chdir, spawn_do_fchdir, spawn_do_closefrom, spawn_do_tcsetpgrp)
  jmp rax
unknown_file_action:
  mov eax, 22 // EINVAL
  jmp failed_eax_in_child
refuse_dup2_our_pipe:
  mov eax, 9 // EBADF
  jmp failed_eax_in_child
renumber_our_pipe:
  mov edi, [rsp + 24]
try_dup:
  mov eax, 32 // __NR_dup
  syscall
  cmp rax, -4 // EINTR
  jz try_dup
  cmp rax, -0xfff
  jae failed_syscall_in_child
  mov [rsp + 24], eax
  mov eax, 3 // __NR_close
  syscall
  jmp file_action_loop
preserve_pipe_during_closefrom:
  mov esi, edi
  mov edi, [rsp + 24]
try_dup2_for_preserve_pipe:
  mov eax, 33 // __NR_dup2
  syscall
  cmp rax, -4 // EINTR
  jz try_dup2_for_preserve_pipe
  cmp rax, rsi
  jnz failed_syscall_in_child
  mov [rsp + 24], eax
  lea r10d, [eax + 1]
  mov eax, 3 // __NR_close
  syscall
  mov edi, r10d
  jmp spawn_do_closefrom_with_edi
  align 2
spawn_do_close:
  endbr64
  mov edi, [r8 + 8]
  cmp edi, [rsp + 24] // Skip attempts to close our write pipe.
  jz next_file_action
  mov eax, 3 // __NR_close
  syscall
  cmp rax, -0xfff
  jb next_file_action
  test edi, edi
  jns next_file_action
  jmp failed_syscall_in_child
  align 2
spawn_do_dup2:
  endbr64
  mov edi, [r8 + 8]
  mov esi, [r8 + 12]
  cmp edi, esi
  jz dup2_is_remove_CLOEXEC
  mov ecx, [rsp + 24]
  cmp edi, ecx
  jz refuse_dup2_our_pipe
  cmp esi, ecx
  jz renumber_our_pipe
try_dup2:
  mov eax, 33 // __NR_dup2
  syscall
  cmp rax, -4 // EINTR
  jz try_dup2
  cmp rax, rsi
  jnz failed_syscall_in_child
  jmp next_file_action
dup2_is_remove_CLOEXEC:
  mov esi, 1 // F_GETFD
  mov eax, 72 // __NR_fcntl
  syscall
  cmp rax, -0xfff
  jae failed_syscall_in_child
  and eax, -2
  mov edx, eax
  mov esi, 2 // F_SETFD
  mov eax, 72 // __NR_fcntl
  syscall
  cmp rax, -0xfff
  jae failed_syscall_in_child
  jmp next_file_action
  align 2
spawn_do_open:
  endbr64
  mov edi, [r8 + 8]
  cmp edi, [rsp + 24]
  jz renumber_our_pipe
  mov eax, 3 // __NR_close
  syscall
  mov rdi, [r8 + 0x10]
  mov esi, [r8 + 0x18]
  mov edx, [r8 + 0x1c]
try_open:
  mov eax, 2 // __NR_open
  syscall
  cmp rax, -4 // EINTR
  jz try_open
  cmp rax, -0xfff
  jae failed_syscall_in_child
  mov esi, [r8 + 8]
  cmp eax, esi
  jz next_file_action
  mov edi, eax
try_dup2_for_open:
  mov eax, 33 // __NR_dup2
  syscall
  cmp rax, -4 // EINTR
  jz try_dup2_for_open
  cmp rax, rsi
  jnz failed_syscall_in_child
  mov eax, 3 // __NR_close
  syscall
  jmp next_file_action
  align 2
spawn_do_chdir:
  endbr64
  mov rdi, [r8 + 8]
try_chdir:
  mov eax, 80 // __NR_chdir
  syscall
  cmp rax, -4 // EINTR
  jz try_chdir
  cmp rax, -0xfff
  jae failed_syscall_in_child
  jmp next_file_action
  align 2
spawn_do_fchdir:
  endbr64
  mov edi, [r8 + 8]
try_fchdir:
  mov eax, 81 // __NR_fchdir
  syscall
  cmp rax, -4 // EINTR
  jz try_fchdir
  cmp rax, -0xfff
  jae failed_syscall_in_child
  jmp next_file_action
  align 2
spawn_do_closefrom:
  endbr64
  mov edi, [r8 + 8]
  cmp edi, [rsp + 24]
  jbe preserve_pipe_during_closefrom
  mov r10d, edi
spawn_do_closefrom_with_edi:
  xor edx, edx
  lea esi, [edx - 1]
try_closerange:
  mov eax, 436 // __NR_close_range
  syscall
  cmp rax, -4 // EINTR
  jz try_closerange
  cmp rax, -0xfff
  jb next_file_action
  lea rdi, str_proc_self_fd
  mov esi, 0x10000 // O_DIRECTORY
try_open_for_closerange:
  mov eax, 2 // __NR_open
  syscall
  cmp rax, -4 // EINTR
  jz try_open_for_closerange
  cmp rax, -0xfff
  jae failed_syscall_in_child
  call closefrom_using_dirfd
  test rax, rax
  jae failed_syscall_in_child
  jmp next_file_action
  align 2
spawn_do_tcsetpgrp:
  endbr64
  xor edi, edi
  mov eax, 121 // __NR_getpgid
  syscall
  lea rdx, [rsp - 8]
  mov edi, [r8 + 8]
  mov esi, 0x5410 // TIOCSPGRP
  mov [rdx], rax
  mov eax, 16 // __NR_ioctl
  syscall
  cmp rax, -0xfff
  jae failed_syscall_in_child
next_file_action:
  add r8, 0x20
  cmp r8, r9
  jnz file_action_loop
done_all_file_actions:

  // Set FD_CLOEXEC on our write pipe.
  // Done after file action handling, as we might have needed to dup2 said pipe.
  mov edi, [rsp + 24]
  mov edx, 1 // FD_CLOEXEC
  lea esi, [edx + 1] // F_SETFD
  mov eax, 72 // __NR_fcntl
  syscall
  cmp rax, -0xfff
  jae failed_syscall_in_child

  // Install the final sigmask.
  xor edx, edx
  lea r10d, [edx + 8]
  lea edi, [edx + 2]
  lea rsi, [rbp - 64]
  mov eax, 14 // __NR_rt_sigprocmask
  syscall
  cmp rax, -0xfff
  jae failed_syscall_in_child

  // Finally do the exec.
  test byte [rbp - 55], 0x01
  mov rdi, [rbp - 16] // char* filename
  mov rsi, [rbp - 40] // char** argv
  mov rdx, [rbp - 48] // char** envp
  mov eax, 59 // __NR_execve (or flags for execvpe_internal)
  jnz want_execvpe
  syscall
  jmp failed_syscall_in_child
want_execvpe:
  call execvpe_internal

failed_syscall_in_child:
  mov ecx, 10 // ECHILD
  neg eax
  cmovz eax, ecx
failed_eax_in_child:
  lea rsi, [rsp + 28]
  mov edi, [rsi - 4]
  mov [rsi], eax
  xor eax, eax
  lea edx, [eax + 4]
try_write:
  sub edx, eax
  jz write_complete
  add rsi, rax
try_write_no_add:
  mov eax, 1 // __NR_write
  syscall
  test rax, rax
  jg try_write
  cmp eax, -4 // EINTR
  jz try_write_no_add
write_complete:

  // Now exit
  mov edi, 127
  mov eax, 60 // __NR_exit
  syscall
  int3
}

const variable str_default_PATH {
  asciiz "/bin:/usr/bin"
}

const variable str_bin_sh {
  asciiz "/bin/sh"
}

const variable str_PATH {
  asciiz "PATH"
}

extern function getenv linkname("libc.so.6::getenv@GLIBC_2.2.5");

public function execvpe {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  xor eax, eax
  call execvpe_internal
  pop.cfi rbp
  jmp syscall_errno
}

function execvpe_internal {
  push.cfi rbp
  mov.cfi rbp, rsp
  push.cfi rbx
  mov ebx, eax // Save entry flags (ah must be 0, al zero if ENOEXEC should be handled by prepending /bin/sh to argv)
  push.cfi r12
  mov r12, rdi // Store filename in r12
  push.cfi r13
  mov r13, rsi // Store argv in r13
  push.cfi r14
  mov r14, rdx // Store envp in r14 (for duration of getenv call)
  push 0
  
  test rdi, rdi
  jz err_ENOENT // Filename is NULL?
  // Scan filename for '/', also compute its length.
  mov rsi, rdi
  mov r8, rsp // r8 holds PATH iterator
scan_filename_loop:
  lodsb
  cmp al, '/'
  jz path_entry_loop // Found '/' in filename? If so, use "" as PATH, no need to copy filename to stack.
  test al, al
  jnz scan_filename_loop
  sub rsi, rdi
  // rsi is now strlen(filename)+1; check it for validity.
  cmp rsi, 256
  ja err_ENAMETOOLONG
  cmp rsi, 1
  jbe err_ENOENT
  // Copy filename to stack, update r12 to point at the copy.
  mov ecx, esi
  neg rsi
  lea rdi, [rsp + rsi + 1]
  and rdi, -16
  mov rsp, rdi
  mov rsi, r12
  mov r12, rdi
  rep movsb

  // Do getenv
  lea rdi, str_PATH
  call getenv
  test rax, rax
  lea r8, str_default_PATH
  cmovnz r8, rax
  mov rdx, r14 // Put envp back in rdx (for the execve syscalls).
  sub rsp, 8
  mov byte ptr [r12 - 1], '/' // Put a '/' before the filename on the stack.

path_entry_loop:
  mov rsi, r8
scan_path_entry:
  lodsb // al = *rsi++
  cmp al, ':'
  jz done_scan_path_entry
  test al, al
  jnz scan_path_entry
done_scan_path_entry:
  mov rcx, rsi
  sub rcx, r8 // rcx is length of this PATH entry, plus one.
  xchg rsi, r8 // Change rsi back to base address of this PATH entry, update r8 to base address of next PATH entry.
  mov rdi, r12 // Filename for execve, in case of empty_path_entry.
  cmp rcx, 4097
  ja next_path_entry
  // Copy path entry to stack underneath filename.
  mov rax, r12
  sub rax, rcx
  sub ecx, 1
  jz empty_path_entry
  mov rdi, rax
  mov r14, rax // Want this in rdi for the syscall, but cannot put it there yet.
  and rax, -8
  mov rsp, rax
  rep movsb // while (ecx) *rdi++ = *rsi++
  // Try the syscall
  mov rdi, r14
empty_path_entry:
  mov rsi, r13 // Saved argv.
  mov eax, 59 // __NR_execve
  syscall
  cmp rax, -8 // ENOEXEC
  jz maybe_try_script
done_try_script:
  cmp rax, -2 // ENOENT
  jz next_path_entry
  cmp rax, -20 // ENOTDIR
  jz next_path_entry
  cmp rax, -13 // EACCES
  jz seen_eaccess
  cmp rax, -116 // ESTALE
  jz next_path_entry
  cmp rax, -110 // ETIMEDOUT
  jz next_path_entry
  cmp rax, -19 // ENODEV
  jz next_path_entry
  jmp fail
seen_eaccess:
  mov bh, al
next_path_entry:
  test byte [r8 - 1], -1
  jnz path_entry_loop // PATH iterator not at end?
  // Reached end of PATH without finding anything.
  // If we ever saw EACCES (saved into bh) then return EACCES.
  // Otherwise return the error from the final execve.
  test bh, bh
  jz fail
  mov al, bh
fail:
  cfi_remember_state
  lea rsp, [rbp - 32]
  pop.cfi r14
  pop.cfi r13
  pop.cfi r12
  pop.cfi rbx
  pop.cfi rbp
  ret
  cfi_restore_state
err_ENOENT:
  mov eax, -2 // ENOENT
  jmp fail
err_ENAMETOOLONG:
  mov eax, -36 // ENAMETOOLONG
  jmp fail
maybe_try_script:
  test bl, bl
  jnz fail // Mode flag does not allow prepending /bin/sh to argv?
  mov r14, rdi // Save the filename; it'll later become argv[1].
  xor eax, eax
  mov rdi, r13 // Saved argv.
  lea rcx, [rbp - 40] // Address of the `push 0` on the stack; used if argv was NULL.
  test rdi, rdi
  cmovz rdi, rcx
  lea rcx, [rax - 1]
  repne scasq // do --rcx while (*rdi++ != rax);
  lea rsi, [rdi - 8] // rsi is now the last entry in argv (which is the NULL entry)
  lea rdi, [rsp - 8] // We'll be copying the last entry to here.
  lea rsp, [rsp + rcx * 8] // Allocate stack space for new argv.
  not rcx // rcx is now length of argv (including the NULL terminator entry)
  cmp rcx, 2
  adc rcx, -1 // Ignore old argv[0] (unless it was the NULL terminator)
  std
  rep movsq // while (rcx--) *rdi-- = *rsi--
  cld
  lea rsi, [rdi - 8] // argv for syscall
  mov [rdi], r14 // Saved filename is argv[1].
  lea rdi, str_bin_sh // filename for syscall
  mov [rsi], rdi // argv[0] is same as filename
  mov eax, 59 // __NR_execve
  syscall
  jmp done_try_script
}

const variable str_proc_self_fd {
  asciiz "/proc/self/fd/"
}

const variable closefrom_fail_msg {
  asciiz "*** closefrom failed to close a file descriptor ***: terminated\n"
}

public function closefrom {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  xor edx, edx
  test edi, edi
  cmovs edi, edx
  lea esi, [edx - 1]
  mov eax, 436 // __NR_close_range
  syscall
  cmp rax, -0xfff
  jb done
  mov r10d, edi
  lea rdi, str_proc_self_fd
  mov esi, 0x10000 // O_DIRECTORY
try_open:
  mov eax, 2 // __NR_open
  syscall
  cmp rax, -4 // EINTR
  jz try_open
  cmp rax, -0xfff
  jb got_dirfd
  neg eax
  mov ecx, 0x1801000 // EMFILE, ENFILE, ENOMEM
  cmp eax, 31
  ja fail
  bt ecx, eax
  jnc fail
  mov edi, r10d
try_close:
  mov eax, 3 // __NR_close
  syscall
  sub edi, -1
  jle done_close
  cmp rax, -9 // EBADF
  jz try_close
done_close:
  lea rdi, str_proc_self_fd
try_open_again:
  mov eax, 2 // __NR_open
  syscall
  cmp rax, -4 // EINTR
  jz try_open_again
  cmp rax, -0xfff
  jae fail
got_dirfd:
  call closefrom_using_dirfd
  test rax, rax
  jnz fail
done:
  cfi_remember_state
  pop rbp
  ret

fail:
  cfi_restore_state
  lea rdi, closefrom_fail_msg
  jmp libc_fatal
}

function closefrom_using_dirfd {
  // `dirfd` in eax, `from` in r10d
  // Preserves r8 and r9, which is useful for spawn_do_closefrom.
  // Closes `dirfd`.
  // Returns (in rax) result of first failing syscall, or 0 if none failed.
  push.cfi rbp
  mov.cfi rbp, rsp
  push.cfi rbx
  mov ebx, eax
  sub rsp, 1024
getdents:
  mov edi, ebx
  mov rsi, rsp
  mov edx, 1024
  mov eax, 217 // __NR_getdents64
  syscall
  test rax, rax
  jle done
  lea rdx, [rsi + rax]
one_dent:
  movzx rcx, word ptr [rsi + 0x10]
  lea rax, [rsi + 0x13]
  add rsi, rcx
  xor edi, edi
  movzx ecx, byte ptr [rax]
  sub ecx, '0'
  cmp ecx, 10
  jae next_dent
scan_int_loop:
  lea edi, [edi+edi*4]
  add rax, 1
  lea edi, [ecx+edi*2]
  movzx ecx, byte ptr [rax]
  sub ecx, '0'
  cmp ecx, 10
  jb scan_int_loop
  cmp ecx, -'0'
  jnz next_dent
  cmp edi, ebx
  jz next_dent
  cmp edi, r10d
  jb next_dent
  mov eax, 3 // __NR_close
  syscall
  bts rbx, 63
next_dent:
  cmp rsi, rdx
  jb one_dent
  btr rbx, 63
  jnc getdents
  mov edi, ebx
  xor esi, esi
  xor edx, edx
  mov eax, 8 // __NR_lseek
  syscall
  cmp rax, -0xfff
  jb getdents
done:
  mov edi, ebx
  mov rbx, rax
  mov eax, 3 // __NR_close
  syscall
  add rsp, 1024
  mov rax, rbx
  pop.cfi rbx
  pop.cfi rbp
  ret
}

extern function free linkname("libc.so.6::free@GLIBC_2.2.5");

public function posix_spawn_file_actions_destroy {
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push.cfi rbx
  push.cfi r12
  mov ebx, dword ptr [rdi + 4]
  mov r12, qword ptr [rdi + 8]
  shl rbx, 5
  jz done
  add rbx, r12
loop:
  sub rbx, 32
  mov ecx, dword ptr [rbx]
  xor ecx, 3
  cmp ecx, 2
  jae nothing_to_free
  mov rdi, [rbx + rcx * 8 + 8]
  call free
nothing_to_free:
  cmp rbx, r12
  jnz loop
done:
  mov rdi, r12
  pop.cfi r12
  pop.cfi rbx
  call free
  xor eax, eax
  pop.cfi rbp
  ret
}

extern function need_posix_spawn_polyfill impose_rename("posix_spawn@GLIBC_2.15", "polyfill::posix_spawn");
extern function need_posix_spawnp_polyfill impose_rename("posix_spawnp@GLIBC_2.15", "polyfill::posix_spawnp");

function need_posix_spawn_polyfills {
  phantom_ref need_posix_spawn_polyfill
  phantom_ref need_posix_spawnp_polyfill
}

function _posix_spawn_file_actions_add_common {
  phantom_ref need_posix_spawn_polyfills

  mov eax, dword ptr [rdi + 4]
  cmp eax, dword ptr [rdi]
  jz need_to_grow
ok:
  lea ecx, [eax + 1]
  shl rax, 5
  mov dword ptr [rdi + 4], ecx
  add rax, qword ptr [rdi + 8]
  ret
need_to_grow:
  push.cfi rbp
  mov.cfi rbp, rsp
  push rsi
  push rdi
  mov rdi, qword ptr [rdi + 8]
  lea esi, [eax + 8]
  shl rsi, 5
  call realloc
  test rax, rax
  jz fail
  pop rdi
  pop rsi
  mov qword ptr [rdi + 8], rax
  mov eax, dword ptr [rdi + 4]
  lea ecx, [eax + 8]
  mov dword ptr [rdi], ecx
  cfi_remember_state
  pop.cfi rbp
  jmp ok
fail:
  cfi_restore_state
  mov eax, 12 // ENOMEM
  leave.cfi
  ret
}

extern function need_posix_spawn_file_actions_destroy_polyfill impose_rename("posix_spawn_file_actions_destroy@GLIBC_2.2.5", "polyfill::posix_spawn_file_actions_destroy");
extern function strdup linkname("libc.so.6::strdup@GLIBC_2.2.5");

public function posix_spawn_file_actions_addchdir_np {
  phantom_ref need_posix_spawn_file_actions_destroy_polyfill
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push rdi // Just for alignment
  push rdi
  mov rdi, rsi
  call strdup
  test rax, rax
  jz fail
  pop rdi
  mov rsi, rax
  pop rax
  call _posix_spawn_file_actions_add_common
  cmp eax, 12
  jz free_then_fail
  mov dword ptr [rax], 3 // spawn_do_chdir
  mov qword ptr [rax + 8], rsi
  xor eax, eax
  cfi_remember_state
  pop.cfi rbp
  ret

free_then_fail:
  cfi_restore_state
  mov rdi, rsi
  call free
fail:
  mov eax, 12 // ENOMEM
  leave.cfi
  ret
}

public function posix_spawn_file_actions_addfchdir_np {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  call _posix_spawn_file_actions_add_common
  cmp eax, 12
  jz done
  mov dword ptr [rax], 4 // spawn_do_fchdir
  mov dword ptr [rax + 8], esi
  xor eax, eax
done:
  pop.cfi rbp
  ret
}

public function posix_spawn_file_actions_addclosefrom_np {
  endbr64
  test esi, esi
  js bad_fd
  push.cfi rbp
  mov rbp, rsp
  call _posix_spawn_file_actions_add_common
  cmp eax, 12
  jz done
  mov dword ptr [rax], 5 // spawn_do_closefrom
  mov dword ptr [rax + 8], esi
  xor eax, eax
done:
  pop.cfi rbp
  ret
bad_fd:
  mov eax, 9 // EBADF
  ret
}

public function posix_spawn_file_actions_addtcsetpgrp_np {
  endbr64
  test esi, esi
  js bad_fd
  push.cfi rbp
  mov rbp, rsp
  call _posix_spawn_file_actions_add_common
  cmp eax, 12
  jz done
  mov dword ptr [rax], 6 // spawn_do_tcsetpgrp
  mov dword ptr [rax + 8], esi
  xor eax, eax
done:
  pop.cfi rbp
  ret
bad_fd:
  mov eax, 9 // EBADF
  ret
}

public function posix_spawnattr_getcgroup_np {
  endbr64
  mov eax, dword ptr [rdi + 0x110]
  mov dword ptr [rsi], eax
  xor eax, eax
  ret
}

public function posix_spawnattr_setcgroup_np {
  phantom_ref need_posix_spawn_polyfills
  endbr64
  mov dword ptr [rdi + 0x110], esi
  xor eax, eax
  ret
}
