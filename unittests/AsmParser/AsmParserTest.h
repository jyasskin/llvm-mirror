// llvm/unittest/AsmParser/AsmParserTest.h - AsmParser test helpers -*- C++ -*-/
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UNITTESTS_ASMPARSER_ASMPARSERTEST_H
#define LLVM_UNITTESTS_ASMPARSER_ASMPARSERTEST_H

#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Assembly/Parser.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

namespace llvm {

class AsmParserTest : public ::testing::Test {
protected:
  // Returns a new Module.
  Module *Parse(const char *AsmString) {
    OwningPtr<Module> Result(new Module("Parsed", Context));
    SMDiagnostic Errors;
    std::string ErrorStr;
    if (!ParseAssemblyString(AsmString, Result.get(), Errors, Context)) {
      Errors.Print("", raw_string_ostream(ErrorStr) << "");
      ADD_FAILURE() << ErrorStr;
      return NULL;
    }
    if (verifyModule(*Result, ReturnStatusAction, &ErrorStr)) {
      ADD_FAILURE() << ErrorStr;
      return NULL;
    }
    return Result.take();
  }

  std::string ParseError(const char *AsmString) {
    OwningPtr<Module> Result(new Module("Parsed", Context));
    SMDiagnostic Errors;
    if (ParseAssemblyString(AsmString, Result.get(), Errors, Context))
      return "<parse succeeded>";
    else
      return Errors.getMessage();
  }

  LLVMContext Context;
};

}  // end namespace llvm

#endif  // LLVM_UNITTESTS_ASMPARSER_ASMPARSERTEST_H
