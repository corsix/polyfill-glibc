public function arc4random_uniform {
  // https://dotat.at/@/2022-04-20-really-divisionless.html
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push.cfi rbx
  mov ebx, edi
  push.cfi r12
  push.cfi r13
  call arc4random
  mul ebx // edx:eax = eax * ebx
  mov r12d, edx // save high part, either it or it plus one will be our result
loop:
  not eax
  cmp ebx, eax
  jbe done
  mov r13d, eax
  call arc4random
  mul ebx // edx:eax = eax * ebx
  cmp r13d, edx
  jz loop
  adc r12d, 0 // add one if edx > r13d
done:
  pop.cfi r13
  mov eax, r12d
  pop.cfi r12
  pop.cfi rbx
  pop.cfi rbp
  ret
}

public function arc4random {
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  sub rsp, 16
  mov esi, 4
  mov rdi, rsp
  call arc4random_buf
  mov eax, dword ptr [rsp]
  leave.cfi
  ret
}

variable arc4random_buf_done_init {
  byte 0
}

const variable str_dev_urandom {
  asciiz "/dev/urandom"
}

const variable str_dev_random {
  asciiz "/dev/random"
}

const variable arc4random_buf_fail_msg {
  asciiz "Fatal glibc error: cannot get entropy for arc4random\n"
}

public function arc4random_buf {
  endbr64
  // Start by trying the getrandom syscall.
  xor eax, eax
  xor edx, edx
try_getrandom:
  sub rsi, rax
  jz done
  add rdi, rax
try_getrandom_no_add:
  mov eax, 318 // __NR_getrandom
  syscall
  test rax, rax
  jg try_getrandom
  cmp eax, -4 // EINTR
  jz try_getrandom_no_add
  cmp eax, -38 // ENOSYS
  jnz hard_fail

  mov r8, rdi
  mov r9, rsi
  // Ensure we've checked for /dev/urandom initialisation.
  test byte ptr [&arc4random_buf_done_init], -1
  jz do_init
done_init:

  // Open /dev/urandom, read from it, close it.
  lea rdi, str_dev_urandom
  mov esi, 0x80100 // O_RDONLY | O_CLOEXEC | O_NOCTTY
try_open:
  mov eax, 2 // __NR_open
  syscall
  cmp rax, -4 // EINTR
  jz try_open
  cmp rax, -0xfff
  jae hard_fail
  mov edi, eax
  mov rsi, r8
  mov rdx, r9
  xor eax, eax
try_read:
  sub rdx, rax
  jz done_read
  add rsi, rax
try_read_no_add:
  xor eax, eax // __NR_read
  syscall
  test rax, rax
  jg try_read
  cmp eax, -4 // EINTR
  jz try_read_no_add
hard_fail:
  lea rdi, arc4random_buf_fail_msg
  jmp libc_fatal
done_read:
  mov eax, 3 // __NR_close
  syscall
done:
  ret

  // Open /dev/random, poll it for readability, close it.
do_init:
  lea rdi, str_dev_random
  mov esi, 0x80100 // O_RDONLY | O_CLOEXEC | O_NOCTTY
try_init_open:
  mov eax, 2 // __NR_open
  syscall
  cmp rax, -4 // EINTR
  jz try_init_open
  cmp rax, -0xfff
  jae hard_fail
  mov esi, 1
  mov dword ptr [rsp - 8], eax
  mov dword ptr [rsp - 4], esi
  xor edx, edx
  lea rdi, [rsp - 8]
try_poll:
  mov eax, 7 // __NR_poll
  syscall
  cmp rax, -4 // EINTR
  jz try_poll
  cmp rax, -0xfff
  jae hard_fail
  mov eax, 3 // __NR_close
  mov byte ptr [&arc4random_buf_done_init], al
  mov edi, dword ptr [rsp - 8]
  syscall
  jmp done_init
}
