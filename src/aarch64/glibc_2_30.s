public function twalk_r {
  cbz x0, outer_tail // NULL tree?
  cbz x1, outer_tail // NULL callback function pointer?
  paciasp.cfi
  stp.cfi fp, lr, [sp, #-0x20]!
  mov.cfi fp, sp
  stp.cfi x21, x22, [sp, #0x10]
  mov x21, x1 // Save callback function pointer.
  mov x22, x2 // Save callback argument.
  bl recurse
  ldp.cfi x21, x22, [sp, #0x10]
  ldp.cfi fp, lr, [sp], #0x20
  autiasp.cfi
outer_tail:
  ret.cfi

recurse:
  ldp x1, x3, [x0, #8]
  and x1, x1, #-2
  mov x2, x22 // For either leaf call or preorder call.
  orr x1, x1, x3
  cbnz x1, has_children // Has left or right node?
  // Leaf call.
  mov x16, x21
  mov x1, #3
  br x16

has_children:
  // Preorder call.
  paciasp.cfi
  stp.cfi x20, lr, [sp, #-0x10]!
  mov x20, x0
  mov x1, #0
  blr x21
  // Possible left recursion.
  ldr x0, [x20, #8]
  ands x0, x0, #-2
  b.eq done_left
  bl recurse
done_left:
  // Postorder call.
  mov x0, x20
  mov x1, #1
  mov x2, x22
  blr x21
  // Possible right recursion.
  ldr x0, [x20, #16]
  cbz x0, done_right
  bl recurse
done_right:
  // Endorder call.
  mov x0, x20
  mov x1, #2
  mov x2, x22
  ldp.cfi x20, lr, [sp], #0x10
  mov x16, x21
  autiasp.cfi
  br x16
}

public function sem_clockwait_2_30 {
  // TODO
}
