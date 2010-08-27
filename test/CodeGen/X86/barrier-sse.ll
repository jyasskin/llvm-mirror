; RUN: llc < %s -march=x86 -mattr=+sse2 | FileCheck %s

declare void @llvm.memory.barrier( i1 , i1 , i1 , i1 , i1)

define void @test() {
; CHECK-NOT: fence
	call void @llvm.memory.barrier( i1 true,  i1 false, i1 false, i1 false, i1 false)
	call void @llvm.memory.barrier( i1 false, i1 true,  i1 false, i1 false, i1 false)
; CHECK: mfence
	call void @llvm.memory.barrier( i1 false, i1 false, i1 true,  i1 false, i1 false)
; CHECK-NOT: fence
	call void @llvm.memory.barrier( i1 false, i1 false, i1 false, i1 true,  i1 false)

	call void @llvm.memory.barrier( i1 true,  i1 true,  i1 false, i1 false, i1 false)
; CHECK: mfence
	call void @llvm.memory.barrier( i1 true,  i1 false, i1 true,  i1 false, i1 false)
; CHECK-NOT: fence
	call void @llvm.memory.barrier( i1 true,  i1 false, i1 false, i1 true,  i1 false)
; CHECK: mfence
	call void @llvm.memory.barrier( i1 false, i1 true,  i1 true,  i1 false, i1 false)
; CHECK-NOT: fence
	call void @llvm.memory.barrier( i1 false, i1 true,  i1 false, i1 true,  i1 false)
; CHECK: mfence
	call void @llvm.memory.barrier( i1 false, i1 false, i1 true,  i1 true,  i1 false)

; CHECK: mfence
	call void @llvm.memory.barrier( i1 true,  i1 true,  i1 true,  i1 false,  i1 false)
; CHECK-NOT: fence
	call void @llvm.memory.barrier( i1 true,  i1 true,  i1 false,  i1 true,  i1 false)
; CHECK: mfence
	call void @llvm.memory.barrier( i1 true,  i1 false,  i1 true,  i1 true,  i1 false)
; CHECK: mfence
	call void @llvm.memory.barrier( i1 false,  i1 true,  i1 true,  i1 true,  i1 false)


; CHECK: mfence
	call void @llvm.memory.barrier( i1 true, i1 true, i1 true, i1 true , i1 false)
; CHECK-NOT: fence
	call void @llvm.memory.barrier( i1 false, i1 false, i1 false, i1 false , i1 false)
; CHECK: ret
	ret void
}
