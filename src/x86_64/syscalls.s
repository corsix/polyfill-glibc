public function fgetxattr {
  endbr64
  mov r10, rcx
  mov eax, 193
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function flistxattr {
  endbr64
  mov eax, 196
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function fremovexattr {
  endbr64
  mov eax, 199
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function fsetxattr {
  endbr64
  mov r10, rcx
  mov eax, 190
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function getxattr {
  endbr64
  mov r10, rcx
  mov eax, 191
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function lgetxattr {
  endbr64
  mov r10, rcx
  mov eax, 192
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function listxattr {
  endbr64
  mov eax, 194
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function llistxattr {
  endbr64
  mov eax, 195
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function lremovexattr {
  endbr64
  mov eax, 198
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function lsetxattr {
  endbr64
  mov r10, rcx
  mov eax, 189
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function readahead {
  endbr64
  mov eax, 187
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function removexattr {
  endbr64
  mov eax, 197
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function setxattr {
  endbr64
  mov r10, rcx
  mov eax, 188
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function epoll_create {
  endbr64
  mov eax, 213
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function epoll_ctl {
  endbr64
  mov r10, rcx
  mov eax, 233
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function remap_file_pages {
  endbr64
  mov r10, rcx
  mov eax, 216
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function sched_setaffinity_2_3_3 {
  endbr64
  mov rdx, rsi
  mov esi, 128
  mov eax, 203
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function semtimedop {
  endbr64
  mov r10, rcx
  mov eax, 220
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function mq_close {
  endbr64
  mov eax, 3
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function mq_getattr {
  endbr64
  mov rdx, rsi
  xor esi, esi
  mov eax, 245
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function mq_open {
  endbr64
  cmp byte ptr [rdi], '/'
  jnz syscall_EINVAL
  xor r10d, r10d
  test sil, 0x40
  cmovz edx, r10d
  cmovnz r10, rcx
  add rdi, 1
  mov eax, 240
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function mq_setattr {
  endbr64
  mov eax, 245
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function mq_unlink {
  endbr64
  cmp byte ptr [rdi], '/'
  jnz syscall_EINVAL
  add rdi, 1
  mov eax, 241
  syscall
  cmp rax, -0xfff
  jae error
  ret
error:
  cmp eax, -1
  mov ecx, -13
  cmovz eax, ecx
  jmp syscall_errno
}

public function sched_getaffinity_2_3_3 {
  endbr64
  mov rdx, rsi
  mov esi, 128
  jmp sched_getaffinity_2_3_4
}

public function sched_getaffinity_2_3_4 {
  endbr64
  mov r10, rsi
  mov eax, 0x7fffffff
  cmp rsi, rax
  cmova esi, eax
  mov eax, 204
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  cmp eax, -1
  jz tail
  movsx rdi, eax
  xor eax, eax
  sub r10, rdi
  jbe tail
  add rdi, rdx
  mov rcx, r10
  rep rex_w stosb // byte ptr [rdi], rcx, al
tail:
  ret
}

public function sched_setaffinity_2_3_4 {
  endbr64
  mov eax, 203
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function fchownat {
  endbr64
  mov r10, rcx
  mov eax, 260
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function __fxstatat {
  endbr64
  cmp edi, 1
  ja syscall_EINVAL
  mov r10d, r8d
  mov edi, esi
  mov rsi, rdx
  mov rdx, rcx
  mov eax, 262
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function inotify_add_watch {
  endbr64
  mov eax, 254
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function inotify_init {
  endbr64
  mov eax, 253
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function inotify_rm_watch {
  endbr64
  mov eax, 255
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function linkat {
  endbr64
  mov r10, rcx
  mov eax, 265
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function mkdirat {
  endbr64
  mov eax, 258
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function mkfifoat {
  endbr64
  or dh, 16
  xor r10d, r10d
  mov eax, 259
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function readlinkat {
  endbr64
  mov r10, rcx
  mov eax, 267
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function renameat {
  endbr64
  mov r10, rcx
  mov eax, 264
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function symlinkat {
  endbr64
  mov eax, 266
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function unlinkat {
  endbr64
  mov eax, 263
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function unshare {
  endbr64
  mov eax, 272
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function futimens {
  endbr64
  test edi, edi
  js syscall_EBADF
  mov rdx, rsi
  xor r10d, r10d
  xor esi, esi
  mov eax, 280
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function utimensat {
  endbr64
  test rsi, rsi
  jz syscall_EINVAL
  mov r10d, ecx
  mov eax, 280
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function eventfd {
  endbr64
  mov eax, 290
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

extern function __libc_read linkname("libc.so.6::read@GLIBC_2.2.5");

public function eventfd_read {
  endbr64
  push.cfi rbp
  mov rbp, rsp
  mov edx, 8
  call __libc_read
  cmp rax, 8
  setne dl
  movzx eax, dl
  pop.cfi rbp
  neg eax
  ret
}

extern function __libc_write linkname("libc.so.6::write@GLIBC_2.2.5");

public function eventfd_write {
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push rsi
  push rsi
  mov edx, 8
  mov rsi, rsp
  call __libc_write
  cmp rax, 8
  setne dl
  leave.cfi
  movzx eax, dl
  neg eax
  ret
}

public function signalfd {
  endbr64
  mov r10d, edx
  mov edx, 8
  mov eax, 289
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function timerfd_create {
  endbr64
  mov eax, 283
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function timerfd_gettime {
  endbr64
  mov eax, 287
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function timerfd_settime {
  endbr64
  mov r10, rcx
  mov eax, 286
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function dup3 {
  endbr64
  mov eax, 292
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function epoll_create1 {
  endbr64
  mov eax, 291
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function inotify_init1 {
  endbr64
  mov eax, 294
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function pipe2 {
  endbr64
  mov eax, 293
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function fanotify_init {
  endbr64
  mov eax, 300
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function fanotify_mark {
  endbr64
  mov r10d, ecx
  mov eax, 301
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function prlimit {
  endbr64
  mov r10, rcx
  mov eax, 302
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function clock_adjtime {
  endbr64
  mov eax, 305
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function name_to_handle_at {
  endbr64
  mov r10, rcx
  mov eax, 303
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function open_by_handle_at {
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  push rdi
  push rdi
  push rsi
  push rdx
  call pthread_testcancel
  pop rdx
  pop rsi
  pop rdi
  mov eax, 304
  syscall
  leave.cfi
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function setns {
  endbr64
  mov eax, 308
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function syncfs {
  endbr64
  mov eax, 306
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function process_vm_readv {
  endbr64
  mov r10, rcx
  mov eax, 310
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function process_vm_writev {
  endbr64
  mov r10, rcx
  mov eax, 311
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function getentropy {
  endbr64
  cmp rsi, 0x100
  ja err_io
  xor eax, eax
  xor edx, edx
loop:
  sub esi, eax
  jz done
  add rdi, rax
loop_no_add:
  mov eax, 318
  syscall
  test rax, rax
  jg loop
  cmp eax, -4 // EINTR
  jz loop_no_add
  test eax, eax
  jnz err
err_io:
  mov eax, -5 // EIO
err:
  jmp syscall_errno
done:
  xor eax, eax
  ret
}

extern function pthread_testcancel linkname("libpthread.so.0::pthread_testcancel@GLIBC_2.2.5");

public function getrandom {
  endbr64
  push.cfi rbp
  mov.cfi rbp, rsp
  mov eax, 318
  syscall
  push rax
  push rax
  call pthread_testcancel
  mov rax, qword ptr [rsp]
  cmp rax, -0xfff
  jae err
  cfi_remember_state
  leave.cfi
  ret
err:
  cfi_restore_state
  call __errno_location
  pop rcx
  neg ecx
  mov dword ptr [rax], ecx
  leave.cfi
  or rax, -1
  ret
}

public function memfd_create {
  endbr64
  mov eax, 319
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function mlock2 {
  endbr64
  test edx, edx
  jz no_flags
  mov eax, 325
  syscall
  cmp rax, -0xfff
  jae error
  ret
error:
  cmp eax, -38
  mov ecx, -22
  cmovz eax, ecx
  jmp syscall_errno
no_flags:
  mov eax, 149
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function pkey_alloc {
  endbr64
  mov eax, 330
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function pkey_free {
  endbr64
  mov eax, 331
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function pkey_get {
  endbr64
  cmp edi, 15
  ja syscall_EINVAL
  xor ecx, ecx
  rdpkru
  lea ecx, [edi+edi]
  shr eax, cl
  and eax, 3
  ret
}

public function pkey_mprotect {
  endbr64
  cmp ecx, -1
  mov r10d, ecx
  mov ecx, 10
  mov eax, 329
  cmovz eax, ecx
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function pkey_set {
  endbr64
  cmp edi, 15
  ja syscall_EINVAL
  cmp esi, 3
  ja syscall_EINVAL
  xor ecx, ecx
  rdpkru
  lea ecx, [edi+edi]
  ror eax, cl
  and eax, -4
  or eax, esi
  rol eax, cl
  xor ecx, ecx
  wrpkru
  xor eax, eax
  ret
}

public function renameat2 {
  endbr64
  mov r10, rcx
  test r8d, r8d
  jz no_flags
  mov eax, 316
  syscall
  cmp rax, -0xfff
  jae error
  ret
error:
  cmp eax, -38
  mov ecx, -22
  cmovz eax, ecx
  jmp syscall_errno
no_flags:
  mov eax, 264
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function statx {
  endbr64
  mov r10d, ecx
  mov eax, 332
  syscall
  cmp rax, -0xfff
  jae error
  ret
error:
  cmp eax, -38
  jnz syscall_errno
  test edx, 0xffffe6ff
  jnz syscall_EINVAL
  mov r10d, edx
  lea rdx, [r8 + 24]
  mov eax, 262
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  xor eax, eax
  mov ecx, dword ptr [rdx + 56] // st_blksize
  mov esi, dword ptr [rdx + 16] // st_nlink
  mov edi, dword ptr [rdx + 28] // st_uid
  mov dword [rdx - 24], 0x7ff // stx_mask
  mov dword [rdx - 20], ecx // stx_blksize
  mov qword [rdx - 16], rax // stx_attributes
  mov dword [rdx - 8], esi // stx_nlink
  mov dword [rdx - 4], edi // st_uid
  mov rcx, qword ptr [rdx + 40] // st_rdev
  mov r8, qword ptr [rdx + 48] // st_size
  mov r10, qword ptr [rdx + 72] // st_atime
  mov esi, dword ptr [rdx + 80] // st_atime_nsec
  mov r9, qword ptr [rdx + 104] // st_ctime
  mov edi, dword ptr [rdx + 112] // st_ctime_nsec
  mov qword ptr [rdx + 16], r8 // stx_size
  mov qword ptr [rdx + 40], r10 // stx_atime (lo)
  mov qword ptr [rdx + 48], rsi // stx_atime (hi)
  mov qword ptr [rdx + 72], r9 // stx_ctime (lo)
  mov qword ptr [rdx + 80], rdi // stx_ctime (hi)
  mov esi, ecx
  mov rdi, rcx
  shr esi, 8
  shr rdi, 32
  and esi, 0xfff
  and edi, 0xfffff000
  or esi, edi
  mov dword ptr [rdx + 104], esi // stx_rdev_major
  mov esi, ecx
  shr ecx, 12
  mov cl, sil
  mov dword ptr [rdx + 108], ecx // stx_rdev_minor
  mov rcx, qword ptr [rdx] // st_dev
  mov esi, dword ptr [rdx + 32] // st_gid
  movzx edi, word ptr [rdx + 24] // st_mode
  mov r8, qword ptr [rdx + 64] // st_blocks
  mov dword ptr [rdx], esi // stx_gid
  mov dword ptr [rdx + 4], edi // stx_mode
  mov qword ptr [rdx + 24], r8 // stx_blocks
  mov qword ptr [rdx + 32], rax // stx_attributes_mask
  mov qword ptr [rdx + 56], rax // stx_btime (lo)
  mov qword ptr [rdx + 64], rax // stx_btime (hi)
  mov dword ptr [rdx + 100], eax // padding in stx_mtime
  mov esi, ecx
  mov rdi, rcx
  shr esi, 8
  shr rdi, 32
  and esi, 0xfff
  and edi, 0xfffff000
  or esi, edi
  mov dword ptr [rdx + 112], esi // stx_dev_major
  mov esi, ecx
  shr ecx, 12
  mov cl, sil
  mov dword ptr [rdx + 116], ecx // stx_dev_minor
  lea ecx, [eax + 14]
  lea rdi, [rdx + 120]
  rep stosq // qword ptr [rdi], rcx, rax // padding
  ret
}

public function getdents64 {
  endbr64
  mov eax, 0x7fffffff
  cmp rdx, rax
  cmova edx, eax
  mov eax, 217
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function gettid {
  endbr64
  mov eax, 186
  syscall
  ret
}

public function tgkill {
  endbr64
  mov eax, 234
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function fstat {
  endbr64
  mov eax, 5
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function fstatat {
  endbr64
  mov r10d, ecx
  mov eax, 262
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function lstat {
  endbr64
  mov eax, 6
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function stat {
  endbr64
  mov eax, 4
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function mknod {
  endbr64
  mov eax, edx
  cmp rax, rdx
  jne syscall_EINVAL
  mov eax, 133
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function mknodat {
  endbr64
  mov r10d, ecx
  cmp r10, rax
  jne syscall_EINVAL
  mov eax, 259
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function close_range {
  endbr64
  mov eax, 436
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function execveat {
  endbr64
  mov r10, rcx
  mov eax, 322
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function fsconfig {
  endbr64
  mov r10, rcx
  mov eax, 431
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function fsmount {
  endbr64
  mov eax, 432
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function fsopen {
  endbr64
  mov eax, 430
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function fspick {
  endbr64
  mov eax, 433
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function mount_setattr {
  endbr64
  mov r10, rcx
  mov eax, 442
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function move_mount {
  endbr64
  mov r10, rcx
  mov eax, 429
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function open_tree {
  endbr64
  mov eax, 428
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function pidfd_getfd {
  endbr64
  mov eax, 438
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function pidfd_open {
  endbr64
  mov eax, 434
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function pidfd_send_signal {
  endbr64
  mov r10, rcx
  mov eax, 424
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function process_madvise {
  endbr64
  mov r10, rcx
  mov eax, 440
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

public function process_mrelease {
  endbr64
  mov eax, 448
  syscall
  cmp rax, -0xfff
  jae syscall_errno
  ret
}

extern function __errno_location linkname("libc.so.6::__errno_location@GLIBC_2.2.5");

function syscall_EBADF {
  mov eax, -9
  jmp syscall_errno
}

function syscall_EINVAL {
  mov eax, -22
  jmp syscall_errno
}

function syscall_errno {
  push.cfi rbp
  neg eax
  mov.cfi rbp, rsp
  push rax
  push rax
  call __errno_location
  pop rcx
  mov dword ptr [rax], ecx
  leave.cfi
  or rax, -1
  ret
}
