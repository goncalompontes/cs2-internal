; The fixture of the virtualizer's front end.
;
; Every function here is a case the analysis has to decide about, and the
; IR test asserts the verdict of each of them. The functions are named with
; a `vm_` prefix, and the policy of the test selects them with the glob
; `vm_*`.

declare i32 @variadic_callee(i32, ...)
declare void @throwing()
declare i32 @__gxx_personality_v0(...)
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)

; A leaf: the arithmetic of the virtual machine, and nothing else.
define i32 @vm_leaf(i32 %x) {
entry:
  %sum = add i32 %x, 1
  %product = mul i32 %sum, 3
  ret i32 %product
}

; The swapping back-edge: the parallel copy has to push every source and
; pop every destination in the reverse order.
define i32 @vm_swap(i32 %n) {
entry:
  br label %loop

loop:
  %first = phi i32 [ 0, %entry ], [ %second, %loop ]
  %second = phi i32 [ 1, %entry ], [ %first, %loop ]
  %next = add i32 %first, %second
  %done = icmp slt i32 %next, %n
  br i1 %done, label %loop, label %exit

exit:
  %result = phi i32 [ %next, %loop ]
  ret i32 %result
}

; A float: the virtual machine handles integers and pointers only.
define i32 @vm_float(float %x) {
entry:
  %doubled = fmul float %x, 2.000000e+00
  %result = fptosi float %doubled to i32
  ret i32 %result
}

; A variadic callee: the indirect call cannot reproduce the variadic ABI.
define i32 @vm_variadic(i32 %x) {
entry:
  %call = call i32 (i32, ...) @variadic_callee(i32 %x, i32 %x)
  ret i32 %call
}

; An indirect call: there is no relocatable address for the pool.
define i32 @vm_indirect(ptr %target, i32 %x) {
entry:
  %call = call i32 %target(i32 %x)
  ret i32 %call
}

; An EH edge: the interpreter cannot model unwinding.
define i32 @vm_eh() personality ptr @__gxx_personality_v0 {
entry:
  invoke void @throwing() to label %ok unwind label %cleanup

ok:
  ret i32 0

cleanup:
  %exception = landingpad { ptr, i32 }
          cleanup
  resume { ptr, i32 } %exception
}

; A call: the call site is recorded, with the signature of the callee.
define i32 @vm_call(i32 %x) {
entry:
  %call = call i32 @vm_leaf(i32 %x)
  ret i32 %call
}

; A `memcpy` whose length is not a constant: it would lower to a CRT call.
define void @vm_memcpy(ptr %destination, ptr %source, i64 %length) {
entry:
  call void @llvm.memcpy.p0.p0.i64(ptr %destination, ptr %source, i64 %length, i1 false)
  ret void
}

; A `memcpy` whose length is a constant: the virtual machine has an opcode
; for it.
define void @vm_memcpy_constant(ptr %destination, ptr %source) {
entry:
  call void @llvm.memcpy.p0.p0.i64(ptr %destination, ptr %source, i64 16, i1 false)
  ret void
}

; A local variable: the local is a byte offset into the frame arena.
define i32 @vm_alloca(i32 %x) {
entry:
  %slot = alloca i32
  store i32 %x, ptr %slot
  %value = load i32, ptr %slot
  ret i32 %value
}
