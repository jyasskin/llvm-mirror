//===- LowerAtomic.cpp - Lower atomic intrinsics --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass lowers atomic intrinsics to non-atomic form for use in a known
// non-preemptible environment.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "loweratomic"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Function.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/Support/IRBuilder.h"
using namespace llvm;

namespace {

bool LowerAtomicLoad(LoadInst *LI) {
  if (!LI->isAtomic())
    return false;
  LI->setNonAtomic();
  return true;
}

bool LowerAtomicStore(StoreInst *SI) {
  if (!SI->isAtomic())
    return false;
  SI->setNonAtomic();
  return true;
}

bool LowerAtomicCmpXchg(AtomicCmpXchgInst *CXI) {
  IRBuilder<> Builder(CXI->getParent(), CXI);
  Value *Ptr = CXI->getPointerOperand();
  Value *Cmp = CXI->getCompareOperand();
  Value *Val = CXI->getNewValOperand();

  LoadInst *Orig = Builder.CreateLoad(Ptr);
  Value *Equal = Builder.CreateICmpEQ(Orig, Cmp);
  Value *Res = Builder.CreateSelect(Equal, Val, Orig);
  Builder.CreateStore(Res, Ptr);

  CXI->replaceAllUsesWith(Orig);
  CXI->eraseFromParent();
  return true;
}

bool LowerAtomicRMW(AtomicRMWInst *RMWI) {
  IRBuilder<> Builder(RMWI->getParent(), RMWI);
  Value *Ptr = RMWI->getPointerOperand();
  Value *Val = RMWI->getValOperand();

  LoadInst *Orig = Builder.CreateLoad(Ptr);
  Value *Res = NULL;

  switch (RMWI->getOperation()) {
  default: llvm_unreachable("Unexpected RMW operation");
  case AtomicRMWInst::Xchg:
    Res = Val;
    break;
  case AtomicRMWInst::Add:
    Res = Builder.CreateAdd(Orig, Val);
    break;
  case AtomicRMWInst::Sub:
    Res = Builder.CreateSub(Orig, Val);
    break;
  case AtomicRMWInst::And:
    Res = Builder.CreateAnd(Orig, Val);
    break;
  case AtomicRMWInst::Nand:
    Res = Builder.CreateNot(Builder.CreateAnd(Orig, Val));
    break;
  case AtomicRMWInst::Or:
    Res = Builder.CreateOr(Orig, Val);
    break;
  case AtomicRMWInst::Xor:
    Res = Builder.CreateXor(Orig, Val);
    break;
  case AtomicRMWInst::Max:
    Res = Builder.CreateSelect(Builder.CreateICmpSLT(Orig, Val),
                               Val, Orig);
    break;
  case AtomicRMWInst::Min:
    Res = Builder.CreateSelect(Builder.CreateICmpSLT(Orig, Val),
                               Orig, Val);
    break;
  case AtomicRMWInst::UMax:
    Res = Builder.CreateSelect(Builder.CreateICmpULT(Orig, Val),
                               Val, Orig);
    break;
  case AtomicRMWInst::UMin:
    Res = Builder.CreateSelect(Builder.CreateICmpULT(Orig, Val),
                               Orig, Val);
    break;
  }
  Builder.CreateStore(Res, Ptr);
  RMWI->replaceAllUsesWith(Orig);
  RMWI->eraseFromParent();
  return true;
}

bool LowerFence(FenceInst *FI) {
  // Fence instructions always become no-ops.
  FI->eraseFromParent();
  return true;
}

struct LowerAtomic : public BasicBlockPass {
  static char ID;
  LowerAtomic() : BasicBlockPass(ID) {}
  bool runOnBasicBlock(BasicBlock &BB) {
    bool Changed = false;
    for (BasicBlock::iterator DI = BB.begin(), DE = BB.end(); DI != DE; ) {
      Instruction *Inst = DI++;
      if (LoadInst *LI = dyn_cast<LoadInst>(Inst))
        Changed |= LowerAtomicLoad(LI);
      else if (StoreInst *SI = dyn_cast<StoreInst>(Inst))
        Changed |= LowerAtomicStore(SI);
      else if (AtomicCmpXchgInst *CXI = dyn_cast<AtomicCmpXchgInst>(Inst))
        Changed |= LowerAtomicCmpXchg(CXI);
      else if (AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(Inst))
        Changed |= LowerAtomicRMW(RMWI);
      else if (FenceInst *FI = dyn_cast<FenceInst>(Inst))
        Changed |= LowerFence(FI);
    }
  }
};

}

char LowerAtomic::ID = 0;
INITIALIZE_PASS(LowerAtomic, "loweratomic",
                "Lower atomic intrinsics to non-atomic form",
                false, false)

Pass *llvm::createLowerAtomicPass() { return new LowerAtomic(); }
