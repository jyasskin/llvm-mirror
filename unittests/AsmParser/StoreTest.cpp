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

class StoreAsmTest : public AsmParserTest {
};

TEST_F(StoreAsmTest, Simple) {
  OwningPtr<Module> M(Parse("define void @f(i32* %a) {"
                            " store i32 0, i32* %a "
                            " ret void "
                            "}"));
  ASSERT_TRUE(M);
  StoreInst *L = dyn_cast<StoreInst>(M->getFunction("f")->begin()->begin());
  ASSERT_TRUE(L);
  EXPECT_FALSE(L->isVolatile());
  EXPECT_EQ(0u, L->getAlignment());
  EXPECT_FALSE(L->isAtomic());
}

TEST_F(StoreAsmTest, Volatile) {
  OwningPtr<Module> M(Parse("define void @f(i32* %a) {"
                            " volatile store i32 0, i32* %a "
                            " ret void "
                            "}"));
  ASSERT_TRUE(M);
  StoreInst *L = dyn_cast<StoreInst>(M->getFunction("f")->begin()->begin());
  ASSERT_TRUE(L);
  EXPECT_TRUE(L->isVolatile());
  EXPECT_EQ(0u, L->getAlignment());
  EXPECT_FALSE(L->isAtomic());
}

TEST_F(StoreAsmTest, Aligned) {
  OwningPtr<Module> M(Parse("define void @f(i32* %a) {"
                            " store i32 0, i32* %a, align 32 "
                            " ret void "
                            "}"));
  ASSERT_TRUE(M);
  StoreInst *L = dyn_cast<StoreInst>(M->getFunction("f")->begin()->begin());
  ASSERT_TRUE(L);
  EXPECT_FALSE(L->isVolatile());
  EXPECT_EQ(32u, L->getAlignment());
  EXPECT_FALSE(L->isAtomic());
}

TEST_F(StoreAsmTest, Atomic) {
  OwningPtr<Module> M(Parse("define void @f(i32* %a) {"
                            " atomic store i32 0, i32* %a unordered "
                            " atomic store i32 0, i32* %a monotonic "
                            " atomic store i32 0, i32* %a acquire "
                            " atomic store i32 0, i32* %a release "
                            " atomic store i32 0, i32* %a acq_rel "
                            " atomic store i32 0, i32* %a seq_cst "
                            " volatile atomic store i32 0, i32* %a acquire "
                            " atomic store i32 0, i32* %a singlethread seq_cst "
                            " volatile atomic store i32 0, i32* %a"
                            "  singlethread monotonic, align 16 "
                            " ret void "
                            "}"));
  ASSERT_TRUE(M);
  BasicBlock::const_iterator I(M->getFunction("f")->begin()->begin());
  const StoreInst *L;
  ASSERT_TRUE((L = dyn_cast<StoreInst>(I)));
  EXPECT_FALSE(L->isVolatile());
  EXPECT_EQ(0u, L->getAlignment());
  EXPECT_TRUE(L->isAtomic());
  EXPECT_EQ(Unordered, L->getOrdering());
  EXPECT_EQ(CrossThread, L->getSynchScope());

  ASSERT_TRUE((L = dyn_cast<StoreInst>(++I)));
  EXPECT_EQ(Monotonic, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<StoreInst>(++I)));
  EXPECT_EQ(Acquire, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<StoreInst>(++I)));
  EXPECT_EQ(Release, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<StoreInst>(++I)));
  EXPECT_EQ(AcquireRelease, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<StoreInst>(++I)));
  EXPECT_EQ(SequentiallyConsistent, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<StoreInst>(++I)));
  EXPECT_TRUE(L->isVolatile());
  EXPECT_EQ(Acquire, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<StoreInst>(++I)));
  EXPECT_EQ(SingleThread, L->getSynchScope());
  EXPECT_EQ(SequentiallyConsistent, L->getOrdering());

  ASSERT_TRUE((L = dyn_cast<StoreInst>(++I)));
  EXPECT_EQ(SingleThread, L->getSynchScope());
  EXPECT_EQ(Monotonic, L->getOrdering());
  EXPECT_EQ(16u, L->getAlignment());
}

}  // end anonymous namespace
}  // end namespace llvm
