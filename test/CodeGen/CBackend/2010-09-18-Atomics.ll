; RUN: llc < %s -march=c | FileCheck %s

define void @foo(i32* %a) {
; CHECK: __sync_fetch_and_add(&(*llvm_cbe_a), 0);
        %la = atomic load i32* %a monotonic
; CHECK: __sync_synchronize();
	fence acquire
; CHECK: __sync_fetch_and_add(&(*llvm_cbe_a), 1u);
        %la2 = atomicrmw add i32* %a, i32 1 monotonic
; CHECK: __sync_synchronize(); __sync_lock_test_and_set(&(*llvm_cbe_a), 2u);
        %la3 = atomicrmw xchg i32* %a, i32 2 monotonic
; CHECK: __sync_val_compare_and_swap(&(*llvm_cbe_a), 2u, 3u);
        %olda = cmpxchg i32* %a, i32 2, i32 3 monotonic
; CHECK: __sync_synchronize(); __sync_lock_test_and_set(&(*llvm_cbe_a), 4u);
        atomic store i32 4, i32* %a monotonic
        ret void
}
