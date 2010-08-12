//===- llvm/unittest/VMCore/InstructionsTest.cpp - Instructions unit tests ===//
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

class LoadAsmTest : public AsmParserTest {
};

TEST_F(LoadAsmTest, Simple) {
  OwningPtr<Module> M(Parse("define void @f(i32* %a) {"
                            " %b = load i32* %a "
                            " ret void "
                            "}"));
  ASSERT_TRUE(M);
  LoadInst *L = dyn_cast<LoadInst>(M->getFunction("f")->begin()->begin());
  ASSERT_TRUE(L);
  EXPECT_EQ("b", L->getName());
  EXPECT_FALSE(L->isVolatile());
  EXPECT_EQ(0u, L->getAlignment());
  EXPECT_FALSE(L->isAtomic());
}

TEST_F(LoadAsmTest, Volatile) {
  OwningPtr<Module> M(Parse("define void @f(i32* %a) {"
                            " %b = volatile load i32* %a "
                            " ret void "
                            "}"));
  ASSERT_TRUE(M);
  LoadInst *L = dyn_cast<LoadInst>(M->getFunction("f")->begin()->begin());
  ASSERT_TRUE(L);
  EXPECT_EQ("b", L->getName());
  EXPECT_TRUE(L->isVolatile());
  EXPECT_EQ(0u, L->getAlignment());
  EXPECT_FALSE(L->isAtomic());
}

TEST_F(LoadAsmTest, Aligned) {
  OwningPtr<Module> M(Parse("define void @f(i32* %a) {"
                            " %b = load i32* %a, align 32 "
                            " ret void "
                            "}"));
  ASSERT_TRUE(M);
  LoadInst *L = dyn_cast<LoadInst>(M->getFunction("f")->begin()->begin());
  ASSERT_TRUE(L);
  EXPECT_EQ("b", L->getName());
  EXPECT_FALSE(L->isVolatile());
  EXPECT_EQ(32u, L->getAlignment());
  EXPECT_FALSE(L->isAtomic());
}

TEST_F(LoadAsmTest, Atomic) {
  OwningPtr<Module> M(Parse("define void @f(i32* %a) {"
                            " %b = atomic load i32* %a unordered "
                            " %c = atomic load i32* %a monotonic "
                            " %d = atomic load i32* %a acquire "
                            " %e = atomic load i32* %a release "
                            " %f = atomic load i32* %a acq_rel "
                            " %g = atomic load i32* %a seq_cst "
                            " %h = volatile atomic load i32* %a acquire "
                            " %i = atomic load i32* %a singlethread seq_cst "
                            " %j = volatile atomic load i32* %a"
                            "  singlethread monotonic, align 16 "
                            " ret void "
                            "}"));
  ASSERT_TRUE(M);
  BasicBlock::const_iterator I(M->getFunction("f")->begin()->begin());
  const LoadInst *L;
  ASSERT_TRUE((L = dyn_cast<LoadInst>(I)));
  EXPECT_EQ("b", L->getName());
  EXPECT_FALSE(L->isVolatile());
  EXPECT_EQ(0u, L->getAlignment());
  EXPECT_TRUE(L->isAtomic());
  EXPECT_EQ(Unordered, L->getOrdering());
  EXPECT_EQ(CrossThread, L->getSynchScope());

  ASSERT_TRUE((L = dyn_cast<LoadInst>(++I)));
  EXPECT_EQ("c", L->getName());
  EXPECT_EQ(Monotonic, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<LoadInst>(++I)));
  EXPECT_EQ("d", L->getName());
  EXPECT_EQ(Acquire, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<LoadInst>(++I)));
  EXPECT_EQ("e", L->getName());
  EXPECT_EQ(Release, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<LoadInst>(++I)));
  EXPECT_EQ("f", L->getName());
  EXPECT_EQ(AcquireRelease, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<LoadInst>(++I)));
  EXPECT_EQ("g", L->getName());
  EXPECT_EQ(SequentiallyConsistent, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<LoadInst>(++I)));
  EXPECT_EQ("h", L->getName());
  EXPECT_TRUE(L->isVolatile());
  EXPECT_EQ(Acquire, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<LoadInst>(++I)));
  EXPECT_EQ("i", L->getName());
  EXPECT_EQ(SingleThread, L->getSynchScope());
  EXPECT_EQ(SequentiallyConsistent, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<LoadInst>(++I)));
  EXPECT_EQ("j", L->getName());
  EXPECT_EQ(SingleThread, L->getSynchScope());
  EXPECT_EQ(Monotonic, L->getOrdering());
  EXPECT_EQ(16u, L->getAlignment());
}

}  // end anonymous namespace
}  // end namespace llvm
