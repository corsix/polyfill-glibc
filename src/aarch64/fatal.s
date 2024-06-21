extern function abort linkname("libc.so.6::abort@GLIBC_2.17");

function libc_fatal {
  // String in x0. No vararg support here.
  // Step 1: Compute string length.
  add x1, x0, #1
len_loop:
  ldrb w2, [x0], #1
  cbnz w2, len_loop
  sub w2, w0, w1
  // Step 2: Write string to stderr.
  sub x3, x0, #1
  mov x0, #0
  mov x8, #0x40 // __NR_write
write_loop:
  subs w2, w2, w0
  b.eq write_done
write_loop_no_add:
  sub x1, x3, x2
  mov w0, #2
  svc #0
  cmp x0, #0
  b.gt write_loop
  cmp w0, #-4 // EINTR
  b.eq write_loop_no_add
write_done:
  // Step 3: Abort.
  b abort
}
