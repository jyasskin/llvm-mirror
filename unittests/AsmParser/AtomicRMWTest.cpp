//===- llvm/unittest/AsmParser/AtomicRMWTest.cpp - atomicrmw tests --------===//
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

class AtomicRMWAsmTest : public AsmParserTest {
};

TEST_F(AtomicRMWAsmTest, Simple) {
  OwningPtr<Module> M(Parse("define void @f(i32* %a) {"
                            " %b = atomicrmw add i32* %a, i32 42 unordered "
                            " ret void"
                            "}"));
  ASSERT_TRUE(M);
  Value *Arg = M->getFunction("f")->arg_begin();
  AtomicRMWInst *RMW =
    dyn_cast<AtomicRMWInst>(M->getFunction("f")->begin()->begin());
  ASSERT_TRUE(RMW);
  EXPECT_EQ("b", RMW->getName());
  EXPECT_EQ(AtomicRMWInst::Add, RMW->getOperation());
  EXPECT_FALSE(RMW->isVolatile());
  EXPECT_EQ(0u, RMW->getAlignment());
  EXPECT_EQ(Unordered, RMW->getOrdering());
  EXPECT_EQ(CrossThread, RMW->getSynchScope());
  EXPECT_EQ(Arg, RMW->getOperand(0));
  EXPECT_EQ(0U, AtomicRMWInst::getPointerOperandIndex());
  EXPECT_EQ(Arg, RMW->getPointerOperand());
  EXPECT_EQ(ConstantInt::get(Type::getInt32Ty(Context), 42),
            RMW->getOperand(1));
  EXPECT_EQ(ConstantInt::get(Type::getInt32Ty(Context), 42),
            RMW->getValOperand());
}

TEST_F(AtomicRMWAsmTest, Operations) {
  OwningPtr<Module> M(
    Parse("define void @f(i32* %a) {"
          " %b = atomicrmw xchg i32* %a, i32 1 seq_cst "
          " %c = atomicrmw add i32* %a, i32 1 seq_cst "
          " %d = atomicrmw sub i32* %a, i32 1 seq_cst "
          " %e = atomicrmw and i32* %a, i32 1 seq_cst "
          " %f = atomicrmw nand i32* %a, i32 1 seq_cst "
          " %g = atomicrmw or i32* %a, i32 1 seq_cst "
          " %h = atomicrmw xor i32* %a, i32 1 seq_cst "
          " %i = atomicrmw max i32* %a, i32 1 seq_cst "
          " %j = atomicrmw min i32* %a, i32 1 seq_cst "
          " %k = atomicrmw umax i32* %a, i32 1 seq_cst "
          " %l = atomicrmw umin i32* %a, i32 1 seq_cst "
          " ret void "
          "}"));
  ASSERT_TRUE(M);
  BasicBlock::const_iterator I(M->getFunction("f")->begin()->begin());
  const AtomicRMWInst *RMW;
  ASSERT_TRUE((RMW = dyn_cast<AtomicRMWInst>(I)));
  EXPECT_EQ("b", RMW->getName());
  EXPECT_EQ(AtomicRMWInst::Xchg, RMW->getOperation());
  EXPECT_FALSE(RMW->isVolatile());
  EXPECT_EQ(0u, RMW->getAlignment());
  EXPECT_EQ(SequentiallyConsistent, RMW->getOrdering());
  EXPECT_EQ(CrossThread, RMW->getSynchScope());

  ASSERT_TRUE((RMW = dyn_cast<AtomicRMWInst>(++I)));
  EXPECT_EQ("c", RMW->getName());
  EXPECT_EQ(AtomicRMWInst::Add, RMW->getOperation());

  ASSERT_TRUE((RMW = dyn_cast<AtomicRMWInst>(++I)));
  EXPECT_EQ("d", RMW->getName());
  EXPECT_EQ(AtomicRMWInst::Sub, RMW->getOperation());

  ASSERT_TRUE((RMW = dyn_cast<AtomicRMWInst>(++I)));
  EXPECT_EQ("e", RMW->getName());
  EXPECT_EQ(AtomicRMWInst::And, RMW->getOperation());

  ASSERT_TRUE((RMW = dyn_cast<AtomicRMWInst>(++I)));
  EXPECT_EQ("f", RMW->getName());
  EXPECT_EQ(AtomicRMWInst::Nand, RMW->getOperation());

  ASSERT_TRUE((RMW = dyn_cast<AtomicRMWInst>(++I)));
  EXPECT_EQ("g", RMW->getName());
  EXPECT_EQ(AtomicRMWInst::Or, RMW->getOperation());

  ASSERT_TRUE((RMW = dyn_cast<AtomicRMWInst>(++I)));
  EXPECT_EQ("h", RMW->getName());
  EXPECT_EQ(AtomicRMWInst::Xor, RMW->getOperation());

  ASSERT_TRUE((RMW = dyn_cast<AtomicRMWInst>(++I)));
  EXPECT_EQ("i", RMW->getName());
  EXPECT_EQ(AtomicRMWInst::Max, RMW->getOperation());

  ASSERT_TRUE((RMW = dyn_cast<AtomicRMWInst>(++I)));
  EXPECT_EQ("j", RMW->getName());
  EXPECT_EQ(AtomicRMWInst::Min, RMW->getOperation());

  ASSERT_TRUE((RMW = dyn_cast<AtomicRMWInst>(++I)));
  EXPECT_EQ("k", RMW->getName());
  EXPECT_EQ(AtomicRMWInst::UMax, RMW->getOperation());

  ASSERT_TRUE((RMW = dyn_cast<AtomicRMWInst>(++I)));
  EXPECT_EQ("l", RMW->getName());
  EXPECT_EQ(AtomicRMWInst::UMin, RMW->getOperation());
}

TEST_F(AtomicRMWAsmTest, Volatile) {
  OwningPtr<Module> M(
    Parse("define void @f(i32* %a) {"
          " %b = volatile atomicrmw add i32* %a, i32 1 acquire "
          " ret void"
          "}"));
  ASSERT_TRUE(M);
  AtomicRMWInst *RMW =
    dyn_cast<AtomicRMWInst>(M->getFunction("f")->begin()->begin());
  ASSERT_TRUE(RMW);
  EXPECT_TRUE(RMW->isVolatile());
  EXPECT_EQ(0u, RMW->getAlignment());
  EXPECT_EQ(Acquire, RMW->getOrdering());
}

TEST_F(AtomicRMWAsmTest, Alignment) {
  OwningPtr<Module> M(
    Parse("define void @f(i32* %a) {"
          " %b = atomicrmw add i32* %a, i32 1 acquire, align 16 "
          " ret void"
          "}"));
  ASSERT_TRUE(M);
  AtomicRMWInst *RMW =
    dyn_cast<AtomicRMWInst>(M->getFunction("f")->begin()->begin());
  ASSERT_TRUE(RMW);
  EXPECT_FALSE(RMW->isVolatile());
  EXPECT_EQ(16u, RMW->getAlignment());
  EXPECT_EQ(Acquire, RMW->getOrdering());
}

}  // end anonymous namespace
}  // end namespace llvm
