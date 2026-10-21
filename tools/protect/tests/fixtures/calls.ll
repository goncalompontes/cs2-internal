; The fixture of the IR test.
;
; "caller" is the function the policy selects. It makes five calls:
;
;   * @target is defined in this module, so the pass rewrites the call into
;     an indirect dispatch through the encrypted descriptor;
;   * @variadic_target is variadic, so the pass leaves the call alone;
;   * @sret_target has an `sret` parameter, so the pass leaves the call
;     alone;
;   * @musttail_target is called in tail position with `musttail`, so the
;     pass leaves the call alone;
;   * @printf is an import, so the pass leaves the call alone.
;
; The pass must touch the first call and nothing else.

declare i32 @printf(ptr, ...)

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
  %b = call i32 (i32, ...) @variadic_target(i32 %x, i32 %x)
  call void @sret_target(ptr sret({ i64, i64, i64 }) %out, i32 %x)
  %d = add i32 %a, %b
  br i1 true, label %tail, label %exit

tail:
  %r = musttail call i32 @musttail_target(i32 %x)
  ret i32 %r

exit:
  ret i32 %d
}
