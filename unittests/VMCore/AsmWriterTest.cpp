//===- llvm/unittest/VMCore/AsmWriterTest.cpp - AsmWriter unit tests ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/LLVMContext.h"
#include "llvm/ADT/OwningPtr.h"
#include "gtest/gtest.h"

namespace llvm {
namespace {

std::string ToString(const Value &V) {
  std::string Result;
  raw_string_ostream OS(Result);
  V.print(OS);
  return OS.str();
}

#define EXPECT_SUBSTRING(NEEDLE, HAYSTACK) \
  EXPECT_TRUE(std::string((HAYSTACK)).find((NEEDLE)) != std::string::npos) \
  << '"' << (NEEDLE) << "\" should be a substring of \"" << (HAYSTACK) << '"';

TEST(AsmWriterTest, Load) {
  LLVMContext C;
  Value *const null_i32p = Constant::getNullValue(Type::getInt32PtrTy(C));
  OwningPtr<LoadInst> LI(new LoadInst(null_i32p, "a"));
  EXPECT_SUBSTRING("%a = load i32* null",
                   ToString(*LI));

  LI.reset(new LoadInst(null_i32p, "a"));
  LI->setVolatile(true);
  EXPECT_SUBSTRING("%a = volatile load i32* null",
                   ToString(*LI));

  LI.reset(new LoadInst(null_i32p, "a"));
  LI->setAtomic(Unordered);
  EXPECT_SUBSTRING("%a = atomic load i32* null unordered",
                   ToString(*LI));

  LI.reset(new LoadInst(null_i32p, "a"));
  LI->setAtomic(Monotonic);
  EXPECT_SUBSTRING("%a = atomic load i32* null monotonic",
                   ToString(*LI));

  LI.reset(new LoadInst(null_i32p, "a"));
  LI->setAtomic(Acquire);
  EXPECT_SUBSTRING("%a = atomic load i32* null acquire",
                   ToString(*LI));

  LI.reset(new LoadInst(null_i32p, "a"));
  LI->setAtomic(SequentiallyConsistent);
  EXPECT_SUBSTRING("%a = atomic load i32* null seq_cst",
                   ToString(*LI));

  LI.reset(new LoadInst(null_i32p, "a"));
  LI->setAtomic(SequentiallyConsistent, SingleThread);
  EXPECT_SUBSTRING("%a = atomic load i32* null singlethread seq_cst",
                   ToString(*LI));

  LI.reset(new LoadInst(null_i32p, "a"));
  LI->setAtomic(SequentiallyConsistent, SingleThread);
  LI->setVolatile(true);
  EXPECT_SUBSTRING("%a = volatile atomic load i32* null singlethread seq_cst",
                   ToString(*LI));

  LI.reset(new LoadInst(null_i32p, "a"));
  LI->setAtomic(SequentiallyConsistent, SingleThread);
  LI->setVolatile(true);
  LI->setAlignment(64);
  EXPECT_SUBSTRING("%a = volatile atomic load i32* null singlethread seq_cst,"
                   " align 64",
                   ToString(*LI));
}

TEST(AsmWriterTest, Store) {
  LLVMContext C;
  Value *const null_i32p = Constant::getNullValue(Type::getInt32PtrTy(C));
  Value *const zero = Constant::getNullValue(Type::getInt32Ty(C));
  OwningPtr<StoreInst> SI(new StoreInst(zero, null_i32p));
  EXPECT_SUBSTRING("store i32 0, i32* null",
                   ToString(*SI));

  SI.reset(new StoreInst(zero, null_i32p));
  SI->setVolatile(true);
  EXPECT_SUBSTRING("volatile store i32 0, i32* null",
                   ToString(*SI));

  SI.reset(new StoreInst(zero, null_i32p));
  SI->setAtomic(Unordered);
  EXPECT_SUBSTRING("atomic store i32 0, i32* null unordered",
                   ToString(*SI));

  SI.reset(new StoreInst(zero, null_i32p));
  SI->setAtomic(Monotonic);
  EXPECT_SUBSTRING("atomic store i32 0, i32* null monotonic",
                   ToString(*SI));

  SI.reset(new StoreInst(zero, null_i32p));
  SI->setAtomic(Release);
  EXPECT_SUBSTRING("atomic store i32 0, i32* null release",
                   ToString(*SI));

  SI.reset(new StoreInst(zero, null_i32p));
  SI->setAtomic(SequentiallyConsistent);
  EXPECT_SUBSTRING("atomic store i32 0, i32* null seq_cst",
                   ToString(*SI));

  SI.reset(new StoreInst(zero, null_i32p));
  SI->setAtomic(SequentiallyConsistent, SingleThread);
  EXPECT_SUBSTRING("atomic store i32 0, i32* null singlethread seq_cst",
                   ToString(*SI));

  SI.reset(new StoreInst(zero, null_i32p));
  SI->setAtomic(SequentiallyConsistent, SingleThread);
  SI->setVolatile(true);
  EXPECT_SUBSTRING("volatile atomic store i32 0, i32* null singlethread seq_cst",
                   ToString(*SI));

  SI.reset(new StoreInst(zero, null_i32p));
  SI->setAtomic(SequentiallyConsistent, SingleThread);
  SI->setVolatile(true);
  SI->setAlignment(64);
  EXPECT_SUBSTRING("volatile atomic store i32 0, i32* null singlethread seq_cst,"
                   " align 64",
                   ToString(*SI));
}

TEST(AsmWriterTest, CmpXchg) {
  LLVMContext C;
  Value *const null_i32p = Constant::getNullValue(Type::getInt32PtrTy(C));
  Value *const zero = Constant::getNullValue(Type::getInt32Ty(C));
  OwningPtr<AtomicCmpXchgInst> CXI(
    new AtomicCmpXchgInst(null_i32p, zero, zero, Unordered, CrossThread));
  EXPECT_SUBSTRING("cmpxchg i32* null, i32 0, i32 0 unordered",
                   ToString(*CXI));

  CXI.reset(new AtomicCmpXchgInst(null_i32p, zero, zero,
                                  Unordered, CrossThread));
  CXI->setVolatile(true);
  EXPECT_SUBSTRING("volatile cmpxchg i32* null, i32 0, i32 0 unordered",
                   ToString(*CXI));

  CXI.reset(new AtomicCmpXchgInst(null_i32p, zero, zero,
                                  AcquireRelease, SingleThread));
  EXPECT_SUBSTRING("cmpxchg i32* null, i32 0, i32 0 singlethread acq_rel",
                   ToString(*CXI));

  CXI.reset(new AtomicCmpXchgInst(null_i32p, zero, zero,
                                  SequentiallyConsistent, SingleThread));
  CXI->setVolatile(true);
  CXI->setAlignment(64);
  EXPECT_SUBSTRING("volatile cmpxchg i32* null, i32 0, i32 0 singlethread"
                   " seq_cst, align 64",
                   ToString(*CXI));
}

TEST(AsmWriterTest, AtomicRMW) {
  LLVMContext C;
  Value *const null_i32p = Constant::getNullValue(Type::getInt32PtrTy(C));
  Value *const zero = Constant::getNullValue(Type::getInt32Ty(C));
  OwningPtr<AtomicRMWInst> RMWI(
    new AtomicRMWInst(AtomicRMWInst::Add, null_i32p, zero,
                      Unordered, CrossThread));
  EXPECT_SUBSTRING("atomicrmw add i32* null, i32 0 unordered",
                   ToString(*RMWI));

  RMWI.reset(new AtomicRMWInst(AtomicRMWInst::Xchg, null_i32p, zero,
                               Unordered, CrossThread));
  RMWI->setVolatile(true);
  EXPECT_SUBSTRING("volatile atomicrmw xchg i32* null, i32 0 unordered",
                   ToString(*RMWI));

  RMWI.reset(new AtomicRMWInst(AtomicRMWInst::And, null_i32p, zero,
                               AcquireRelease, SingleThread));
  EXPECT_SUBSTRING("atomicrmw and i32* null, i32 0 singlethread acq_rel",
                   ToString(*RMWI));

  RMWI.reset(new AtomicRMWInst(AtomicRMWInst::Max, null_i32p, zero,
                                  SequentiallyConsistent, SingleThread));
  RMWI->setVolatile(true);
  RMWI->setAlignment(64);
  EXPECT_SUBSTRING("volatile atomicrmw max i32* null, i32 0 singlethread"
                   " seq_cst, align 64",
                   ToString(*RMWI));
}

TEST(AsmWriterTest, Fence) {
  LLVMContext C;
  OwningPtr<FenceInst> FI(new FenceInst(C, SequentiallyConsistent, CrossThread));
  EXPECT_SUBSTRING("fence seq_cst",
                   ToString(*FI));

  FI.reset(new FenceInst(C, AcquireRelease, SingleThread));
  EXPECT_SUBSTRING("fence singlethread acq_rel",
                   ToString(*FI));
}

}
}
