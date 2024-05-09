public function qsort_r {
  // This is currently a heap sort.
  // Smoothsort or introsort would be better.
  // This simultaneously implements both qsort and qsort_r.

  endbr64
  cmp rsi, 1
  jbe nothing_to_sort // Less than two elements?
  test rdx, rdx
  jz nothing_to_sort // Elements are zero-size?

  push.cfi rbp
  mov.cfi rbp, rsp

  push.cfi r8      // Save comparator context  in [rbp -  8]
  push.cfi rcx     // Save comparator function in [rbp - 16]
  mov ecx, edx
  u8_label_lut(rax, rcx, cycle_final_0, cycle_final_1, cycle_final_2, cycle_final_3, cycle_final_4, cycle_final_5, cycle_final_6, cycle_final_7)
  push.cfi rax     // Save cycle_final_[elem_size & 7] in [rbp - 24]

  // Save non-volatile registers, move our state there.
  push.cfi rbx     // Saved in [rbp - 32]
  mov rbx, rsi // rbx holds iteration variable for heap building (starts at element count divided by two, counts down toward zero during the build phase)
  shr rbx, 1
  push.cfi r12     // Saved in [rbp - 40]
  mov r12, rdx // r12 holds element size
  push.cfi r13     // Saved in [rbp - 48]
  mov r13, rdi // r13 holds base pointer of array to be sorted
  push.cfi r14     // Saved in [rbp - 56]
  mov r14, rsi // r14 holds size of the heap (initially equal to element count, counts down to zero during the drain phase)
  push.cfi r15     // Saved in [rbp - 64]
                   // r15 holds heap index for sift.
  push rax         // Space for cycle pointer #0 in [rbp - 72], which right now is also [rsp].

  // 1st phase builds a heap.
build_head:
  // r15 = (rbx - 1), [rsp] = base + r15 * element_size
  lea rax, [rbx - 1]
  mov r15, rax
  // Ensure that heap element r15 is correctly ordered w.r.t. its children.
  call sift
  // Loop
  sub rbx, 1
  jnz build_head

  // 2nd phase drains a heap.
  sub r14, 1
drain_head:
  // [rsp] = base + --heap_size * element_size
  mov rax, r14
  xor r15, r15
  // Swap *rax with *base, then restore the heap invariants starting at *base.
  call pop_and_sift
  // Loop
  sub r14, 1
  jnz drain_head

  pop rax
  // Restore non-volatile registers.
  cfi_remember_state
  pop.cfi r15
  pop.cfi r14
  pop.cfi r13
  pop.cfi r12
  pop.cfi rbx

  leave.cfi
nothing_to_sort:
  ret

pop_and_sift:
  cfi_restore_state
  // Cycle pointer #1 is array base, r15 = 0.
  push r13
  push r13
sift:
  imul rax, r12
  add rax, r13
  mov qword ptr [rbp - 72], rax
sift_head:
  // Called with a heap index in r15, and pointer corresponding to that index
  // in [rbp - 72]. Ensures that said index is correctly ordered with regards
  // to its children, swapping it with a child if necessary. This is repeated
  // as necessary.
  add r15, 1
  add r15, r15
  jc done_sift // Overflow while computing child index?
  cmp r15, r14
  ja done_sift // Child index is larger than heap size?
  lea r15, [r15 - 1] // Change back to 1st child (without affecting flags).
  jz only_one_child // Only has one child?
  // Compare the two children.
  // rdi (1st outgoing argument) = base + r15 * elem_size
  mov rdi, r12
  imul rdi, r15
  add rdi, r13
  // rsi (2nd outgoing argument) = base + (r15 + 1) * elem_size
  lea rsi, [rdi + r12]
  // rdx (3rd outgoing argument) = comparator context
  mov rdx, qword ptr [rbp - 8]
  // Call comparator
  call qword ptr [rbp - 16]
  // Add one to r15 if 1st child smaller (so that r15 is index of larger child)
  sar eax, 31
  cdqe
  sub r15, rax
only_one_child:
  // rsi (2nd outgoing argument) = base + r15 * elem_size
  mov rsi, r12
  imul rsi, r15
  add rsi, r13
  // rsi is next element in the swap list
  push rsi
  push rsi // Just for stack alignment; could push any value here.
  // rdi (1st outgoing argument) = Pointer to value being sifted
  mov rdi, [rbp - 72]
  // rdx (3rd outgoing argument) = comparator context
  mov rdx, qword ptr [rbp - 8]
  // Call comparator
  call [rbp - 16]
  // Continue sifting if value being sifted is smaller than child
  test eax, eax
  js sift_head
  // Undo the last pair of pushes
  add rsp, 16
done_sift:
  // Sifting is complete, so now the various swaps actually need to be done.
  // The swap chain pointers are at [rbp - 72], [rbp - 88], ..., [rsp + 8].
  // r8 is stack offset of next pointer in swap chain.
  lea r8, [rbp - 88]
  sub r8, rsp
  jb no_cycle_to_apply
  mov r10, r12 // r10 counts from element_size down to zero
  xor rdx, rdx // rdx counts from 0 up to element_size
  sub r10, 64
  jb cycle64_done
cycle64_head:
  mov rdi, [rbp - 72] // rdi is copy dst ptr
  add rdi, rdx
  mov r9, r8 // r9 is loop iteration variable, initially value is r8, decrements by steps of 16 toward zero.
  movups xmm0, [rdi]
  movups xmm1, [rdi + 16]
  movups xmm2, [rdi + 32]
  movups xmm3, [rdi + 48]
cycle64_inner_head:
  mov rsi, [rsp + r9] // rsi is copy src ptr
  add rsi, rdx
  movups xmm4, [rsi]
  movups xmm5, [rsi + 16]
  movups xmm6, [rsi + 32]
  movups xmm7, [rsi + 48]
  movups [rdi], xmm4
  movups [rdi + 16], xmm5
  movups [rdi + 32], xmm6
  movups [rdi + 48], xmm7
  mov rdi, rsi
  sub r9, 16
  jae cycle64_inner_head
  movups [rdi], xmm0
  movups [rdi + 16], xmm1
  movups [rdi + 32], xmm2
  movups [rdi + 48], xmm3
  add rdx, 64
  sub r10, 64
  jae cycle64_head
cycle64_done:
  add r10, 64
  sub r10, 8
  jb cycle8_done
cycle8_head:
  mov rdi, [rbp - 72]
  mov r9, r8 // r9 is loop iteration variable, initially value is r8, decrements by steps of 16 toward zero.
  mov rax, qword ptr [rdi + rdx]
cycle8_inner_head:
  mov rsi, [rsp + r9]
  mov rcx, qword ptr [rsi + rdx]
  mov qword ptr [rdi + rdx], rcx
  mov rdi, rsi
  sub r9, 16
  jae cycle8_inner_head
  mov qword ptr [rdi + rdx], rax
  add rdx, 8
  sub r10, 8
  jae cycle8_head
cycle8_done:
  mov rdi, [rbp - 72]
  jmp [rbp - 24]

align 8
cycle_final_0:
  endbr64
no_cycle_to_apply:
  lea rsp, [rbp - 80]
  ret

align 8
cycle_final_4:
  endbr64
  mov eax, dword ptr [rdi + rdx]
cycle_final_4_inner_head:
  mov rsi, [rsp + r8]
  mov ecx, dword ptr [rsi + rdx]
  mov dword ptr [rdi + rdx], ecx
  mov rdi, rsi
  sub r8, 16
  jae cycle_final_4_inner_head
  mov dword ptr [rsi + rdx], eax
  lea rsp, [rbp - 80]
  ret

align 8
cycle_final_2:
  endbr64
  mov ax, word ptr [rdi + rdx]
cycle_final_2_inner_head:
  mov rsi, [rsp + r8]
  mov cx, word ptr [rsi + rdx]
  mov word ptr [rdi + rdx], cx
  mov rdi, rsi
  sub r8, 16
  jae cycle_final_2_inner_head
  mov word ptr [rsi + rdx], ax
  lea rsp, [rbp - 80]
  ret

align 8
cycle_final_1:
  endbr64
  mov al, byte ptr [rdi + rdx]
cycle_final_1_inner_head:
  mov rsi, [rsp + r8]
  mov cl, byte ptr [rsi + rdx]
  mov byte ptr [rdi + rdx], cl
  mov rdi, rsi
  sub r8, 16
  jae cycle_final_1_inner_head
  mov byte ptr [rsi + rdx], al
  lea rsp, [rbp - 80]
  ret

align 8
cycle_final_3:
  endbr64
  add rdi, rdx
  mov r9w, word ptr [rdi]
  mov r10b, byte ptr [rdi + 2]
cycle_final_3_inner_head:
  mov rsi, [rsp + r8]
  add rsi, rdx
  mov ax, word ptr [rsi]
  mov cl, byte ptr [rsi + 2]
  mov word ptr [rdi], ax
  mov byte ptr [rdi + 2], cl
  mov rdi, rsi
  sub r8, 16
  jae cycle_final_3_inner_head
  mov word ptr [rsi], r9w
  mov byte ptr [rsi + 2], r10b
  lea rsp, [rbp - 80]
  ret

align 8
cycle_final_5:
  endbr64
  add rdi, rdx
  mov r9d, dword ptr [rdi]
  mov r10b, byte ptr [rdi + 4]
cycle_final_5_inner_head:
  mov rsi, [rsp + r8]
  add rsi, rdx
  mov eax, dword ptr [rsi]
  mov cl, byte ptr [rsi + 4]
  mov dword ptr [rdi], eax
  mov byte ptr [rdi + 4], cl
  mov rdi, rsi
  sub r8, 16
  jae cycle_final_5_inner_head
  mov dword ptr [rsi], r9d
  mov byte ptr [rsi + 4], r10b
  lea rsp, [rbp - 80]
  ret

align 8
cycle_final_6:
  endbr64
  add rdi, rdx
  mov r9d, dword ptr [rdi]
  mov r10w, word ptr [rdi + 4]
cycle_final_6_inner_head:
  mov rsi, [rsp + r8]
  add rsi, rdx
  mov eax, dword ptr [rsi]
  mov cx, word ptr [rsi + 4]
  mov dword ptr [rdi], eax
  mov word ptr [rdi + 4], cx
  mov rdi, rsi
  sub r8, 16
  jae cycle_final_6_inner_head
  mov dword ptr [rsi], r9d
  mov word ptr [rsi + 4], r10w
  lea rsp, [rbp - 80]
  ret

align 8
cycle_final_7:
  endbr64
  add rdi, rdx
  movss xmm0, dword ptr [rdi]
  mov r9w, word ptr [rdi + 4]
  mov r10b, byte ptr [rdi + 6]
cycle_final_7_inner_head:
  mov rsi, [rsp + r8]
  add rsi, rdx
  mov eax, dword ptr [rsi]
  mov r11w, word ptr [rsi + 4]
  mov cl, byte ptr [rsi + 6]
  mov dword ptr [rdi], eax
  mov word ptr [rdi + 4], r11w
  mov byte ptr [rdi + 6], cl
  mov rdi, rsi
  sub r8, 16
  jae cycle_final_7_inner_head
  movss dword ptr [rsi], xmm0
  mov word ptr [rsi + 4], r9w
  mov byte ptr [rsi + 6], r10b
  lea rsp, [rbp - 80]
  ret
}
