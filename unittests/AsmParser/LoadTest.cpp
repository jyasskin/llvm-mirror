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

}  // end anonymous namespace
}  // end namespace llvm
