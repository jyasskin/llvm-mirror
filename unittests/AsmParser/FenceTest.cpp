//===- llvm/unittest/AsmParser/FenceTest.cpp - fence instruction tests ----===//
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

class FenceAsmTest : public AsmParserTest {
};

TEST_F(FenceAsmTest, Valid) {
  OwningPtr<Module> M(Parse("define void @f() {"
                            " fence acquire "
                            " fence release "
                            " fence acq_rel "
                            " fence seq_cst "
                            " fence singlethread acquire "
                            " fence singlethread release "
                            " fence singlethread acq_rel "
                            " fence singlethread seq_cst "
                            " ret void"
                            "}"));
  ASSERT_TRUE(M);
  BasicBlock::const_iterator I(M->getFunction("f")->begin()->begin());
  const FenceInst *F;
  ASSERT_TRUE((F = dyn_cast<FenceInst>(I)));
  EXPECT_EQ(Acquire, F->getOrdering());
  EXPECT_EQ(CrossThread, F->getSynchScope());

  ASSERT_TRUE((F = dyn_cast<FenceInst>(++I)));
  EXPECT_EQ(Release, F->getOrdering());
  EXPECT_EQ(CrossThread, F->getSynchScope());

  ASSERT_TRUE((F = dyn_cast<FenceInst>(++I)));
  EXPECT_EQ(AcquireRelease, F->getOrdering());
  EXPECT_EQ(CrossThread, F->getSynchScope());

  ASSERT_TRUE((F = dyn_cast<FenceInst>(++I)));
  EXPECT_EQ(SequentiallyConsistent, F->getOrdering());
  EXPECT_EQ(CrossThread, F->getSynchScope());

  ASSERT_TRUE((F = dyn_cast<FenceInst>(++I)));
  EXPECT_EQ(Acquire, F->getOrdering());
  EXPECT_EQ(SingleThread, F->getSynchScope());

  ASSERT_TRUE((F = dyn_cast<FenceInst>(++I)));
  EXPECT_EQ(Release, F->getOrdering());
  EXPECT_EQ(SingleThread, F->getSynchScope());

  ASSERT_TRUE((F = dyn_cast<FenceInst>(++I)));
  EXPECT_EQ(AcquireRelease, F->getOrdering());
  EXPECT_EQ(SingleThread, F->getSynchScope());

  ASSERT_TRUE((F = dyn_cast<FenceInst>(++I)));
  EXPECT_EQ(SequentiallyConsistent, F->getOrdering());
  EXPECT_EQ(SingleThread, F->getSynchScope());
}

TEST_F(FenceAsmTest, Illegal) {
  EXPECT_EQ("error: fence cannot be unordered",
            ParseError("define void @f() {"
                       " fence unordered "
                       " ret void"
                       "}"));
  EXPECT_EQ("error: fence cannot be monotonic",
            ParseError("define void @f() {"
                       " fence monotonic "
                       " ret void"
                       "}"));
}

}  // end anonymous namespace
}  // end namespace llvm
