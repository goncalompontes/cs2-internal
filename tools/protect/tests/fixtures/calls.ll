; The fixture of the IR test.
;
; "caller" is the function the policy selects. It makes seven calls:
;
;   * @target is defined in this module, so the pass rewrites the call into
;     an indirect dispatch through the encrypted descriptor;
;   * @external_target is a **declaration** (no body in this module): the
;     policy's `cross_module` list names it, so the pass rewrites the call
;     too, and the table holds the symbol itself (a relocatable pointer
;     the linker resolves);
;   * @variadic_target is variadic, so the pass leaves the call alone;
;   * @sret_target has an `sret` parameter, so the pass routes the call
;     through a **thunk**: the thunk carries the `sret` attribute of the
;     call and the hidden pointer, which it takes out of the cell of the
;     scratch that the `sret` attribute names -- the first one here, the
;     callee being a free function;
;   * @external_sret_target is a declaration with an `sret` parameter: the
;     policy's `cross_module` list names it, so the pass routes it through
;     a thunk as well, and the table holds the external symbol;
;   * @musttail_target is called in tail position with `musttail`, so the
;     pass leaves the call alone;
;   * @printf is an import, so the pass leaves the call alone.
;
; The pass must touch the first four calls and nothing else.
;
; The triple is the one the `sret` narrowing was measured on: the rule is
; closed on every other target.

target triple = "x86_64-pc-windows-msvc"

declare i32 @printf(ptr, ...)
declare i32 @external_target(i32 %x)
declare void @external_sret_target(ptr sret({ i64, i64, i64 }) %out, i32 %x)

define i32 @target(i32 %x) {
entry:
  %r = add i32 %x, 1
  ret i32 %r
}

define i32 @variadic_target(i32 %x, ...) {
entry:
  ret i32 %x
}

define i32 @musttail_target(i32 %x) {
entry:
  ret i32 %x
}

define void @sret_target(ptr sret({ i64, i64, i64 }) %out, i32 %x) {
entry:
  %v0 = insertvalue { i64, i64, i64 } undef, i64 1, 0
  %v1 = insertvalue { i64, i64, i64 } %v0, i64 2, 1
  %v2 = insertvalue { i64, i64, i64 } %v1, i64 3, 2
  store { i64, i64, i64 } %v2, ptr %out
  ret void
}

define i32 @caller(i32 %x) {
entry:
  %out = alloca { i64, i64, i64 }, align 8
  %a = call i32 @target(i32 %x)
  %e = call i32 @external_target(i32 %x)
  %b = call i32 (i32, ...) @variadic_target(i32 %x, i32 %x)
  call void @sret_target(ptr sret({ i64, i64, i64 }) %out, i32 %x)
  call void @external_sret_target(ptr sret({ i64, i64, i64 }) %out, i32 %x)
  %d = add i32 %a, %b
  br i1 true, label %tail, label %exit

tail:
  %r = musttail call i32 @musttail_target(i32 %x)
  ret i32 %r

exit:
  ret i32 %d
}
