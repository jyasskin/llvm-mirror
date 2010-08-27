; RUN: llc < %s -march=thumb -mcpu=cortex-a8 | FileCheck %s

declare void @llvm.memory.barrier( i1 , i1 , i1 , i1 , i1 )

define void @t1() {
; CHECK: t1:
; CHECK: dmb
  call void @llvm.memory.barrier( i1 false, i1 false, i1 false, i1 true, i1 false )
  ret void
}
