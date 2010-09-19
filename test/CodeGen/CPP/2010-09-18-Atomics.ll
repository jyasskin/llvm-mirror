; RUN: llc < %s -march=cpp | FileCheck %s

define void @foo(i32* %a) {
; CHECK: new LoadInst(ptr_a, "la", false, label_6);
; CHECK: setAtomic(Monotonic, CrossThread);
        %la = atomic load i32* %a monotonic
; CHECK: new FenceInst(mod->getContext(), Acquire, CrossThread, label_6);
	fence acquire
; CHECK: new AtomicRMWInst(AtomicRMWInst::Add, ptr_a, const_int32_2, SequentiallyConsistent, SingleThread, label_6);
        %la2 = atomicrmw add i32* %a, i32 1 singlethread seq_cst
; CHECK: new AtomicRMWInst(AtomicRMWInst::Xchg, ptr_a, const_int32_3, AcquireRelease, SingleThread, label_6);
        %la3 = atomicrmw xchg i32* %a, i32 2 singlethread acq_rel
; CHECK: new AtomicCmpXchgInst(ptr_a, const_int32_3, const_int32_4, Release, CrossThread, label_6);
        %olda = cmpxchg i32* %a, i32 2, i32 3 release
; CHECK: new StoreInst(const_int32_5, ptr_a, false, label_6);
; CHECK: setAtomic(Unordered, CrossThread);
        atomic store i32 4, i32* %a unordered
        ret void
}
