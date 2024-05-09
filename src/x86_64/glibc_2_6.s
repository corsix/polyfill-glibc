public function __sched_cpucount {
  endbr64
  xor eax, eax // Accumulated count, becomes return value.
  and rdi, -8
  jz done
  add rsi, rdi
  neg rdi
  xor ecx, ecx // Accumulated bits, not yet in count.
ingest_head:
  mov rdx, qword ptr [rsi + rdi]
  test rcx, rdx
  jnz reduce_head
  or rcx, rdx
done_reduce:
  add rdi, 8
  jnz ingest_head
  test rcx, rcx
  jz done
reduce_head:
  lea r8, [rcx - 1]
  add eax, 1
  and rcx, r8
  jnz reduce_head
  mov rcx, rdx
  test rdi, rdi
  jnz done_reduce
done:
  ret
}
