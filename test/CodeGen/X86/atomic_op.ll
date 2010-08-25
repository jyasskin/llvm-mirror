; RUN: llc < %s -march=x86 | FileCheck %s

target datalayout = "e-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:64-f32:32:32-f64:32:64-v64:64:64-v128:128:128-a0:0:64-f80:128:128"

define void @func(i32 %argc, i8** %argv) nounwind {
entry:
	%argc.addr = alloca i32		; <i32*> [#uses=1]
	%argv.addr = alloca i8**		; <i8***> [#uses=1]
	%val1 = alloca i32		; <i32*> [#uses=2]
	%val2 = alloca i32		; <i32*> [#uses=15]
	%andt = alloca i32		; <i32*> [#uses=2]
	%ort = alloca i32		; <i32*> [#uses=2]
	%xort = alloca i32		; <i32*> [#uses=2]
	%old = alloca i32		; <i32*> [#uses=18]
	%temp = alloca i32		; <i32*> [#uses=2]
	store i32 %argc, i32* %argc.addr
	store i8** %argv, i8*** %argv.addr
	store i32 0, i32* %val1
	store i32 31, i32* %val2
	store i32 3855, i32* %andt
	store i32 3855, i32* %ort
	store i32 3855, i32* %xort
	store i32 4, i32* %temp
	%tmp = load i32* %temp
        ; CHECK: lock
        ; CHECK: xaddl
	call i32 @llvm.atomic.load.add.i32.p0i32( i32* %val1, i32 %tmp )		; <i32>:0 [#uses=1]
	store i32 %0, i32* %old
        ; CHECK: lock
        ; CHECK: xaddl
	call i32 @llvm.atomic.load.sub.i32.p0i32( i32* %val2, i32 30 )		; <i32>:1 [#uses=1]
	store i32 %1, i32* %old
        ; CHECK: lock
        ; CHECK: xaddl
	call i32 @llvm.atomic.load.add.i32.p0i32( i32* %val2, i32 1 )		; <i32>:2 [#uses=1]
	store i32 %2, i32* %old
        ; CHECK: lock
        ; CHECK: xaddl
	call i32 @llvm.atomic.load.sub.i32.p0i32( i32* %val2, i32 1 )		; <i32>:3 [#uses=1]
	store i32 %3, i32* %old
        ; CHECK: andl
        ; CHECK: lock
        ; CHECK: cmpxchgl
	call i32 @llvm.atomic.load.and.i32.p0i32( i32* %andt, i32 4080 )		; <i32>:4 [#uses=1]
	store i32 %4, i32* %old
        ; CHECK: orl
        ; CHECK: lock
        ; CHECK: cmpxchgl
	call i32 @llvm.atomic.load.or.i32.p0i32( i32* %ort, i32 4080 )		; <i32>:5 [#uses=1]
	store i32 %5, i32* %old
        ; CHECK: xorl
        ; CHECK: lock
        ; CHECK: cmpxchgl
	call i32 @llvm.atomic.load.xor.i32.p0i32( i32* %xort, i32 4080 )		; <i32>:6 [#uses=1]
	store i32 %6, i32* %old
        ; CHECK: cmov
        ; CHECK: lock
        ; CHECK: cmpxchgl
	call i32 @llvm.atomic.load.min.i32.p0i32( i32* %val2, i32 16 )		; <i32>:7 [#uses=1]
	store i32 %7, i32* %old
	%neg = sub i32 0, 1		; <i32> [#uses=1]
        ; CHECK: cmov
        ; CHECK: lock
        ; CHECK: cmpxchgl
	call i32 @llvm.atomic.load.min.i32.p0i32( i32* %val2, i32 %neg )		; <i32>:8 [#uses=1]
	store i32 %8, i32* %old
        ; CHECK: cmov
        ; CHECK: lock
        ; CHECK: cmpxchgl
	call i32 @llvm.atomic.load.max.i32.p0i32( i32* %val2, i32 1 )		; <i32>:9 [#uses=1]
	store i32 %9, i32* %old
        ; CHECK: cmov
        ; CHECK: lock
        ; CHECK: cmpxchgl
	call i32 @llvm.atomic.load.max.i32.p0i32( i32* %val2, i32 0 )		; <i32>:10 [#uses=1]
	store i32 %10, i32* %old
        ; CHECK: cmov
        ; CHECK: lock
        ; CHECK: cmpxchgl
	call i32 @llvm.atomic.load.umax.i32.p0i32( i32* %val2, i32 65535 )		; <i32>:11 [#uses=1]
	store i32 %11, i32* %old
        ; CHECK: cmov
        ; CHECK: lock
        ; CHECK: cmpxchgl
	call i32 @llvm.atomic.load.umax.i32.p0i32( i32* %val2, i32 10 )		; <i32>:12 [#uses=1]
	store i32 %12, i32* %old
        ; CHECK: cmov
        ; CHECK: lock
        ; CHECK: cmpxchgl
	call i32 @llvm.atomic.load.umin.i32.p0i32( i32* %val2, i32 1 )		; <i32>:13 [#uses=1]
	store i32 %13, i32* %old
        ; CHECK: cmov
        ; CHECK: lock
        ; CHECK: cmpxchgl
	call i32 @llvm.atomic.load.umin.i32.p0i32( i32* %val2, i32 10 )		; <i32>:14 [#uses=1]
	store i32 %14, i32* %old
        ; CHECK: xchgl   %{{.*}}, {{.*}}(%esp)
	call i32 @llvm.atomic.swap.i32.p0i32( i32* %val2, i32 1976 )		; <i32>:15 [#uses=1]
	store i32 %15, i32* %old
	%neg1 = sub i32 0, 10		; <i32> [#uses=1]
        ; CHECK: lock
        ; CHECK: cmpxchgl
	call i32 @llvm.atomic.cmp.swap.i32.p0i32( i32* %val2, i32 %neg1, i32 1 )		; <i32>:16 [#uses=1]
	store i32 %16, i32* %old
        ; CHECK: lock
        ; CHECK: cmpxchgl
	call i32 @llvm.atomic.cmp.swap.i32.p0i32( i32* %val2, i32 1976, i32 1 )		; <i32>:17 [#uses=1]
	store i32 %17, i32* %old
	ret void
}

define void @test2(i32 addrspace(256)* nocapture %P) nounwind {
entry:
; CHECK: lock
; CHECK:	cmpxchgl	%{{.*}}, %gs:(%{{.*}})

  %0 = tail call i32 @llvm.atomic.cmp.swap.i32.p256i32(i32 addrspace(256)* %P, i32 0, i32 1)
  ret void
}

; CHECK: test_load
define void @test_load(i32* %a, i64* %b) {
; CHECK-NOT: fence
; CHECK: movl
; CHECK-NOT: fence
       volatile atomic load i32* %a seq_cst;
; CHECK-NOT: fence
; CHECK-TODO: movq
; CHECK-NOT-TODO: fence
;       volatile atomic load i64* %b singlethread unordered;
; CHECK: ret
       ret void;
}

; CHECK: test_store
define void @test_store(i32* %a, i64* %b) {
; CHECK-NOT: fence
; CHECK: mov
; CHECK-NOT: fence
       volatile atomic store i32 0, i32* %a unordered;
; CHECK-NOT: fence
; CHECK: mov
; CHECK-NOT: fence
       volatile atomic store i32 0, i32* %a monotonic;
; CHECK-NOT: fence
; CHECK: mov
; CHECK-NOT: fence
       volatile atomic store i32 0, i32* %a release;
; CHECK-NOT: fence
; CHECK: movl
; CHECK-NOT: fence
       volatile atomic store i32 0, i32* %a singlethread seq_cst;
; CHECK-NOT: fence
; CHECK-TODO: movq
; CHECK-NOT-TODO: fence
;       volatile atomic store i64 0, i64* %b singlethread seq_cst;
; CHECK-NOT-TODO: fence
; xchg is implicitly locked.
; CHECK: xchg
; CHECK-NOT: fence
       volatile atomic store i32 0, i32* %a seq_cst;
; CHECK-NOT: fence
; CHECK-TODO: lock
; CHECK-NEXT-TODO: cmpxchg8b
; CHECK-NOT-TODO: fence
;       volatile atomic store i64 0, i64* %b seq_cst;
; CHECK: ret
       ret void;
}

; CHECK: test_cmpxchg
define void @test_cmpxchg(i32* %a, i64* %b) {
; CHECK-NOT: fence
; CHECK: movl $1, %eax
; CHECK: lock
; CHECK-NEXT: cmpxchgl
; CHECK-NOT: fence
       volatile cmpxchg i32* %a, i32 1, i32 2 unordered;
; CHECK-NOT: fence
; CHECK: lock
; CHECK-NEXT: cmpxchgl
; CHECK-NOT: fence
       volatile cmpxchg i32* %a, i32 3, i32 4 seq_cst;
; CHECK-NOT: fence
; CHECK-NOT-TODO: lock
; CHECK: cmpxchgl
; CHECK-NOT: fence
       volatile cmpxchg i32* %a, i32 5, i32 6 singlethread seq_cst;

; CHECK-NOT: fence
; CHECK: lock
; CHECK-NEXT: cmpxchg8b
; CHECK-NOT: fence
       volatile cmpxchg i64* %b, i64 1, i64 2 unordered;

; CHECK-NOT: fence
; CHECK: lock
; CHECK-NEXT: cmpxchg8b
; CHECK-NOT: fence
       volatile cmpxchg i64* %b, i64 3, i64 4 seq_cst;

; CHECK-NOT: fence
; CHECK-NOT-TODO: lock
; CHECK: cmpxchg8b
; CHECK-NOT: fence
       volatile cmpxchg i64* %b, i64 5, i64 6 singlethread seq_cst;

; CHECK: ret
       ret void;
}

declare i32 @llvm.atomic.cmp.swap.i32.p256i32(i32 addrspace(256)* nocapture, i32, i32) nounwind

declare i32 @llvm.atomic.load.add.i32.p0i32(i32*, i32) nounwind 

declare i32 @llvm.atomic.load.sub.i32.p0i32(i32*, i32) nounwind 

declare i32 @llvm.atomic.load.and.i32.p0i32(i32*, i32) nounwind 

declare i32 @llvm.atomic.load.or.i32.p0i32(i32*, i32) nounwind 

declare i32 @llvm.atomic.load.xor.i32.p0i32(i32*, i32) nounwind 

declare i32 @llvm.atomic.load.min.i32.p0i32(i32*, i32) nounwind 

declare i32 @llvm.atomic.load.max.i32.p0i32(i32*, i32) nounwind 

declare i32 @llvm.atomic.load.umax.i32.p0i32(i32*, i32) nounwind 

declare i32 @llvm.atomic.load.umin.i32.p0i32(i32*, i32) nounwind 

declare i32 @llvm.atomic.swap.i32.p0i32(i32*, i32) nounwind 

declare i32 @llvm.atomic.cmp.swap.i32.p0i32(i32*, i32, i32) nounwind 
