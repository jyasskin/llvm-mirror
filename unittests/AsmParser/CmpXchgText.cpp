//===- llvm/unittest/AsmParser/CmpXchgTest.cpp - cmpxchg instruction tests ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AsmParserTest.h"
#include "llvm/Instructions.h"

namespace llvm {
namespace {

class CmpXchgAsmTest : public AsmParserTest {
};

TEST_F(CmpXchgAsmTest, Simple) {
  OwningPtr<Module> M(Parse("define void @f(i32* %a) {"
                            " %b = cmpxchg i32* %a, i32 42, i32 7 unordered "
                            " ret void"
                            "}"));
  ASSERT_TRUE(M);
  Value *Arg = M->getFunction("f")->arg_begin();
  AtomicCmpXchgInst *CAS =
    dyn_cast<AtomicCmpXchgInst>(M->getFunction("f")->begin()->begin());
  ASSERT_TRUE(CAS);
  EXPECT_EQ("b", CAS->getName());
  EXPECT_FALSE(CAS->isVolatile());
  EXPECT_EQ(0u, CAS->getAlignment());
  EXPECT_EQ(Unordered, CAS->getOrdering());
  EXPECT_EQ(CrossThread, CAS->getSynchScope());
  EXPECT_EQ(Arg, CAS->getOperand(0));
  EXPECT_EQ(0U, AtomicCmpXchgInst::getPointerOperandIndex());
  EXPECT_EQ(Arg, CAS->getPointerOperand());
  EXPECT_EQ(ConstantInt::get(Type::getInt32Ty(Context), 42),
            CAS->getOperand(1));
  EXPECT_EQ(ConstantInt::get(Type::getInt32Ty(Context), 42),
            CAS->getCompareOperand());
  EXPECT_EQ(ConstantInt::get(Type::getInt32Ty(Context), 7),
            CAS->getOperand(2));
  EXPECT_EQ(ConstantInt::get(Type::getInt32Ty(Context), 7),
            CAS->getNewValOperand());
}

TEST_F(CmpXchgAsmTest, Orderings) {
  OwningPtr<Module> M(Parse("define void @f(i32* %a) {"
                            " %b = cmpxchg i32* %a, i32 0, i32 1 unordered "
                            " %c = cmpxchg i32* %a, i32 0, i32 1 monotonic "
                            " %d = cmpxchg i32* %a, i32 0, i32 1 acquire "
                            " %e = cmpxchg i32* %a, i32 0, i32 1 release "
                            " %f = cmpxchg i32* %a, i32 0, i32 1 acq_rel "
                            " %g = cmpxchg i32* %a, i32 0, i32 1 seq_cst "
                            " %ub = cmpxchg i32* %a, i32 0, i32 1 singlethread unordered "
                            " %ug = cmpxchg i32* %a, i32 0, i32 1 singlethread seq_cst "
                            " ret void "
                            "}"));
  ASSERT_TRUE(M);
  BasicBlock::const_iterator I(M->getFunction("f")->begin()->begin());
  const AtomicCmpXchgInst *CAS;
  ASSERT_TRUE((CAS = dyn_cast<AtomicCmpXchgInst>(I)));
  EXPECT_EQ("b", CAS->getName());
  EXPECT_FALSE(CAS->isVolatile());
  EXPECT_EQ(0u, CAS->getAlignment());
  EXPECT_EQ(Unordered, CAS->getOrdering());
  EXPECT_EQ(CrossThread, CAS->getSynchScope());

  ASSERT_TRUE((CAS = dyn_cast<AtomicCmpXchgInst>(++I)));
  EXPECT_EQ("c", CAS->getName());
  EXPECT_EQ(Monotonic, CAS->getOrdering());

  ASSERT_TRUE((CAS = dyn_cast<AtomicCmpXchgInst>(++I)));
  EXPECT_EQ("d", CAS->getName());
  EXPECT_EQ(Acquire, CAS->getOrdering());

  ASSERT_TRUE((CAS = dyn_cast<AtomicCmpXchgInst>(++I)));
  EXPECT_EQ("e", CAS->getName());
  EXPECT_EQ(Release, CAS->getOrdering());

  ASSERT_TRUE((CAS = dyn_cast<AtomicCmpXchgInst>(++I)));
  EXPECT_EQ("f", CAS->getName());
  EXPECT_EQ(AcquireRelease, CAS->getOrdering());

  ASSERT_TRUE((CAS = dyn_cast<AtomicCmpXchgInst>(++I)));
  EXPECT_EQ("g", CAS->getName());
  EXPECT_EQ(SequentiallyConsistent, CAS->getOrdering());

  ASSERT_TRUE((CAS = dyn_cast<AtomicCmpXchgInst>(++I)));
  EXPECT_EQ("ub", CAS->getName());
  EXPECT_EQ(Unordered, CAS->getOrdering());
  EXPECT_EQ(SingleThread, CAS->getSynchScope());

  ASSERT_TRUE((CAS = dyn_cast<AtomicCmpXchgInst>(++I)));
  EXPECT_EQ("ug", CAS->getName());
  EXPECT_EQ(SequentiallyConsistent, CAS->getOrdering());
  EXPECT_EQ(SingleThread, CAS->getSynchScope());
}

TEST_F(CmpXchgAsmTest, Volatile) {
  OwningPtr<Module> M(Parse("define void @f(i32* %a) {"
                            " %b = volatile cmpxchg i32* %a, i32 0, i32 1 acquire "
                            " ret void"
                            "}"));
  AtomicCmpXchgInst *CAS =
    dyn_cast<AtomicCmpXchgInst>(M->getFunction("f")->begin()->begin());
  ASSERT_TRUE(CAS);
  EXPECT_TRUE(CAS->isVolatile());
  EXPECT_EQ(0u, CAS->getAlignment());
  EXPECT_EQ(Acquire, CAS->getOrdering());
}

TEST_F(CmpXchgAsmTest, Alignment) {
  OwningPtr<Module> M(Parse("define void @f(i32* %a) {"
                            " %b = cmpxchg i32* %a, i32 0, i32 1 acquire, align 16 "
                            " ret void"
                            "}"));
  AtomicCmpXchgInst *CAS =
    dyn_cast<AtomicCmpXchgInst>(M->getFunction("f")->begin()->begin());
  ASSERT_TRUE(CAS);
  EXPECT_FALSE(CAS->isVolatile());
  EXPECT_EQ(16u, CAS->getAlignment());
  EXPECT_EQ(Acquire, CAS->getOrdering());
}

}  // end anonymous namespace
}  // end namespace llvm
