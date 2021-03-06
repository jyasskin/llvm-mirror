//===-- X86AsmParser.cpp - Parse X86 assembly to MCInst instructions ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Target/TargetAsmParser.h"
#include "X86.h"
#include "X86Subtarget.h"
#include "llvm/Target/TargetRegistry.h"
#include "llvm/Target/TargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace {
struct X86Operand;

class X86ATTAsmParser : public TargetAsmParser {
  MCAsmParser &Parser;
  TargetMachine &TM;

protected:
  unsigned Is64Bit : 1;

private:
  MCAsmParser &getParser() const { return Parser; }

  MCAsmLexer &getLexer() const { return Parser.getLexer(); }

  bool Error(SMLoc L, const Twine &Msg) { return Parser.Error(L, Msg); }

  bool ParseRegister(unsigned &RegNo, SMLoc &StartLoc, SMLoc &EndLoc);

  X86Operand *ParseOperand();
  X86Operand *ParseMemOperand(unsigned SegReg, SMLoc StartLoc);

  bool ParseDirectiveWord(unsigned Size, SMLoc L);

  bool MatchAndEmitInstruction(SMLoc IDLoc,
                               SmallVectorImpl<MCParsedAsmOperand*> &Operands,
                               MCStreamer &Out);

  /// @name Auto-generated Matcher Functions
  /// {

#define GET_ASSEMBLER_HEADER
#include "X86GenAsmMatcher.inc"

  /// }

public:
  X86ATTAsmParser(const Target &T, MCAsmParser &_Parser, TargetMachine &TM)
    : TargetAsmParser(T), Parser(_Parser), TM(TM) {

    // Initialize the set of available features.
    setAvailableFeatures(ComputeAvailableFeatures(
                           &TM.getSubtarget<X86Subtarget>()));
  }

  virtual bool ParseInstruction(StringRef Name, SMLoc NameLoc,
                                SmallVectorImpl<MCParsedAsmOperand*> &Operands);

  virtual bool ParseDirective(AsmToken DirectiveID);
};

class X86_32ATTAsmParser : public X86ATTAsmParser {
public:
  X86_32ATTAsmParser(const Target &T, MCAsmParser &_Parser, TargetMachine &TM)
    : X86ATTAsmParser(T, _Parser, TM) {
    Is64Bit = false;
  }
};

class X86_64ATTAsmParser : public X86ATTAsmParser {
public:
  X86_64ATTAsmParser(const Target &T, MCAsmParser &_Parser, TargetMachine &TM)
    : X86ATTAsmParser(T, _Parser, TM) {
    Is64Bit = true;
  }
};

} // end anonymous namespace

/// @name Auto-generated Match Functions
/// {

static unsigned MatchRegisterName(StringRef Name);

/// }

namespace {

/// X86Operand - Instances of this class represent a parsed X86 machine
/// instruction.
struct X86Operand : public MCParsedAsmOperand {
  enum KindTy {
    Token,
    Register,
    Immediate,
    Memory
  } Kind;

  SMLoc StartLoc, EndLoc;

  union {
    struct {
      const char *Data;
      unsigned Length;
    } Tok;

    struct {
      unsigned RegNo;
    } Reg;

    struct {
      const MCExpr *Val;
    } Imm;

    struct {
      unsigned SegReg;
      const MCExpr *Disp;
      unsigned BaseReg;
      unsigned IndexReg;
      unsigned Scale;
    } Mem;
  };

  X86Operand(KindTy K, SMLoc Start, SMLoc End)
    : Kind(K), StartLoc(Start), EndLoc(End) {}

  /// getStartLoc - Get the location of the first token of this operand.
  SMLoc getStartLoc() const { return StartLoc; }
  /// getEndLoc - Get the location of the last token of this operand.
  SMLoc getEndLoc() const { return EndLoc; }

  virtual void dump(raw_ostream &OS) const {}

  StringRef getToken() const {
    assert(Kind == Token && "Invalid access!");
    return StringRef(Tok.Data, Tok.Length);
  }
  void setTokenValue(StringRef Value) {
    assert(Kind == Token && "Invalid access!");
    Tok.Data = Value.data();
    Tok.Length = Value.size();
  }

  unsigned getReg() const {
    assert(Kind == Register && "Invalid access!");
    return Reg.RegNo;
  }

  const MCExpr *getImm() const {
    assert(Kind == Immediate && "Invalid access!");
    return Imm.Val;
  }

  const MCExpr *getMemDisp() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.Disp;
  }
  unsigned getMemSegReg() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.SegReg;
  }
  unsigned getMemBaseReg() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.BaseReg;
  }
  unsigned getMemIndexReg() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.IndexReg;
  }
  unsigned getMemScale() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.Scale;
  }

  bool isToken() const {return Kind == Token; }

  bool isImm() const { return Kind == Immediate; }

  bool isImmSExti16i8() const {
    if (!isImm())
      return false;

    // If this isn't a constant expr, just assume it fits and let relaxation
    // handle it.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE)
      return true;

    // Otherwise, check the value is in a range that makes sense for this
    // extension.
    uint64_t Value = CE->getValue();
    return ((                                  Value <= 0x000000000000007FULL)||
            (0x000000000000FF80ULL <= Value && Value <= 0x000000000000FFFFULL)||
            (0xFFFFFFFFFFFFFF80ULL <= Value && Value <= 0xFFFFFFFFFFFFFFFFULL));
  }
  bool isImmSExti32i8() const {
    if (!isImm())
      return false;

    // If this isn't a constant expr, just assume it fits and let relaxation
    // handle it.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE)
      return true;

    // Otherwise, check the value is in a range that makes sense for this
    // extension.
    uint64_t Value = CE->getValue();
    return ((                                  Value <= 0x000000000000007FULL)||
            (0x00000000FFFFFF80ULL <= Value && Value <= 0x00000000FFFFFFFFULL)||
            (0xFFFFFFFFFFFFFF80ULL <= Value && Value <= 0xFFFFFFFFFFFFFFFFULL));
  }
  bool isImmSExti64i8() const {
    if (!isImm())
      return false;

    // If this isn't a constant expr, just assume it fits and let relaxation
    // handle it.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE)
      return true;

    // Otherwise, check the value is in a range that makes sense for this
    // extension.
    uint64_t Value = CE->getValue();
    return ((                                  Value <= 0x000000000000007FULL)||
            (0xFFFFFFFFFFFFFF80ULL <= Value && Value <= 0xFFFFFFFFFFFFFFFFULL));
  }
  bool isImmSExti64i32() const {
    if (!isImm())
      return false;

    // If this isn't a constant expr, just assume it fits and let relaxation
    // handle it.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE)
      return true;

    // Otherwise, check the value is in a range that makes sense for this
    // extension.
    uint64_t Value = CE->getValue();
    return ((                                  Value <= 0x000000007FFFFFFFULL)||
            (0xFFFFFFFF80000000ULL <= Value && Value <= 0xFFFFFFFFFFFFFFFFULL));
  }

  bool isMem() const { return Kind == Memory; }

  bool isAbsMem() const {
    return Kind == Memory && !getMemSegReg() && !getMemBaseReg() &&
      !getMemIndexReg() && getMemScale() == 1;
  }

  bool isReg() const { return Kind == Register; }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    // Add as immediates when possible.
    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::CreateImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::CreateExpr(Expr));
  }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::CreateReg(getReg()));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }

  void addMemOperands(MCInst &Inst, unsigned N) const {
    assert((N == 5) && "Invalid number of operands!");
    Inst.addOperand(MCOperand::CreateReg(getMemBaseReg()));
    Inst.addOperand(MCOperand::CreateImm(getMemScale()));
    Inst.addOperand(MCOperand::CreateReg(getMemIndexReg()));
    addExpr(Inst, getMemDisp());
    Inst.addOperand(MCOperand::CreateReg(getMemSegReg()));
  }

  void addAbsMemOperands(MCInst &Inst, unsigned N) const {
    assert((N == 1) && "Invalid number of operands!");
    Inst.addOperand(MCOperand::CreateExpr(getMemDisp()));
  }

  static X86Operand *CreateToken(StringRef Str, SMLoc Loc) {
    X86Operand *Res = new X86Operand(Token, Loc, Loc);
    Res->Tok.Data = Str.data();
    Res->Tok.Length = Str.size();
    return Res;
  }

  static X86Operand *CreateReg(unsigned RegNo, SMLoc StartLoc, SMLoc EndLoc) {
    X86Operand *Res = new X86Operand(Register, StartLoc, EndLoc);
    Res->Reg.RegNo = RegNo;
    return Res;
  }

  static X86Operand *CreateImm(const MCExpr *Val, SMLoc StartLoc, SMLoc EndLoc){
    X86Operand *Res = new X86Operand(Immediate, StartLoc, EndLoc);
    Res->Imm.Val = Val;
    return Res;
  }

  /// Create an absolute memory operand.
  static X86Operand *CreateMem(const MCExpr *Disp, SMLoc StartLoc,
                               SMLoc EndLoc) {
    X86Operand *Res = new X86Operand(Memory, StartLoc, EndLoc);
    Res->Mem.SegReg   = 0;
    Res->Mem.Disp     = Disp;
    Res->Mem.BaseReg  = 0;
    Res->Mem.IndexReg = 0;
    Res->Mem.Scale    = 1;
    return Res;
  }

  /// Create a generalized memory operand.
  static X86Operand *CreateMem(unsigned SegReg, const MCExpr *Disp,
                               unsigned BaseReg, unsigned IndexReg,
                               unsigned Scale, SMLoc StartLoc, SMLoc EndLoc) {
    // We should never just have a displacement, that should be parsed as an
    // absolute memory operand.
    assert((SegReg || BaseReg || IndexReg) && "Invalid memory operand!");

    // The scale should always be one of {1,2,4,8}.
    assert(((Scale == 1 || Scale == 2 || Scale == 4 || Scale == 8)) &&
           "Invalid scale!");
    X86Operand *Res = new X86Operand(Memory, StartLoc, EndLoc);
    Res->Mem.SegReg   = SegReg;
    Res->Mem.Disp     = Disp;
    Res->Mem.BaseReg  = BaseReg;
    Res->Mem.IndexReg = IndexReg;
    Res->Mem.Scale    = Scale;
    return Res;
  }
};

} // end anonymous namespace.


bool X86ATTAsmParser::ParseRegister(unsigned &RegNo,
                                    SMLoc &StartLoc, SMLoc &EndLoc) {
  RegNo = 0;
  const AsmToken &TokPercent = Parser.getTok();
  assert(TokPercent.is(AsmToken::Percent) && "Invalid token kind!");
  StartLoc = TokPercent.getLoc();
  Parser.Lex(); // Eat percent token.

  const AsmToken &Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Identifier))
    return Error(Tok.getLoc(), "invalid register name");

  // FIXME: Validate register for the current architecture; we have to do
  // validation later, so maybe there is no need for this here.
  RegNo = MatchRegisterName(Tok.getString());

  // If the match failed, try the register name as lowercase.
  if (RegNo == 0)
    RegNo = MatchRegisterName(LowercaseString(Tok.getString()));

  // FIXME: This should be done using Requires<In32BitMode> and
  // Requires<In64BitMode> so "eiz" usage in 64-bit instructions
  // can be also checked.
  if (RegNo == X86::RIZ && !Is64Bit)
    return Error(Tok.getLoc(), "riz register in 64-bit mode only");

  // Parse "%st" as "%st(0)" and "%st(1)", which is multiple tokens.
  if (RegNo == 0 && (Tok.getString() == "st" || Tok.getString() == "ST")) {
    RegNo = X86::ST0;
    EndLoc = Tok.getLoc();
    Parser.Lex(); // Eat 'st'

    // Check to see if we have '(4)' after %st.
    if (getLexer().isNot(AsmToken::LParen))
      return false;
    // Lex the paren.
    getParser().Lex();

    const AsmToken &IntTok = Parser.getTok();
    if (IntTok.isNot(AsmToken::Integer))
      return Error(IntTok.getLoc(), "expected stack index");
    switch (IntTok.getIntVal()) {
    case 0: RegNo = X86::ST0; break;
    case 1: RegNo = X86::ST1; break;
    case 2: RegNo = X86::ST2; break;
    case 3: RegNo = X86::ST3; break;
    case 4: RegNo = X86::ST4; break;
    case 5: RegNo = X86::ST5; break;
    case 6: RegNo = X86::ST6; break;
    case 7: RegNo = X86::ST7; break;
    default: return Error(IntTok.getLoc(), "invalid stack index");
    }

    if (getParser().Lex().isNot(AsmToken::RParen))
      return Error(Parser.getTok().getLoc(), "expected ')'");

    EndLoc = Tok.getLoc();
    Parser.Lex(); // Eat ')'
    return false;
  }

  // If this is "db[0-7]", match it as an alias
  // for dr[0-7].
  if (RegNo == 0 && Tok.getString().size() == 3 &&
      Tok.getString().startswith("db")) {
    switch (Tok.getString()[2]) {
    case '0': RegNo = X86::DR0; break;
    case '1': RegNo = X86::DR1; break;
    case '2': RegNo = X86::DR2; break;
    case '3': RegNo = X86::DR3; break;
    case '4': RegNo = X86::DR4; break;
    case '5': RegNo = X86::DR5; break;
    case '6': RegNo = X86::DR6; break;
    case '7': RegNo = X86::DR7; break;
    }

    if (RegNo != 0) {
      EndLoc = Tok.getLoc();
      Parser.Lex(); // Eat it.
      return false;
    }
  }

  if (RegNo == 0)
    return Error(Tok.getLoc(), "invalid register name");

  EndLoc = Tok.getLoc();
  Parser.Lex(); // Eat identifier token.
  return false;
}

X86Operand *X86ATTAsmParser::ParseOperand() {
  switch (getLexer().getKind()) {
  default:
    // Parse a memory operand with no segment register.
    return ParseMemOperand(0, Parser.getTok().getLoc());
  case AsmToken::Percent: {
    // Read the register.
    unsigned RegNo;
    SMLoc Start, End;
    if (ParseRegister(RegNo, Start, End)) return 0;
    if (RegNo == X86::EIZ || RegNo == X86::RIZ) {
      Error(Start, "eiz and riz can only be used as index registers");
      return 0;
    }

    // If this is a segment register followed by a ':', then this is the start
    // of a memory reference, otherwise this is a normal register reference.
    if (getLexer().isNot(AsmToken::Colon))
      return X86Operand::CreateReg(RegNo, Start, End);


    getParser().Lex(); // Eat the colon.
    return ParseMemOperand(RegNo, Start);
  }
  case AsmToken::Dollar: {
    // $42 -> immediate.
    SMLoc Start = Parser.getTok().getLoc(), End;
    Parser.Lex();
    const MCExpr *Val;
    if (getParser().ParseExpression(Val, End))
      return 0;
    return X86Operand::CreateImm(Val, Start, End);
  }
  }
}

/// ParseMemOperand: segment: disp(basereg, indexreg, scale).  The '%ds:' prefix
/// has already been parsed if present.
X86Operand *X86ATTAsmParser::ParseMemOperand(unsigned SegReg, SMLoc MemStart) {

  // We have to disambiguate a parenthesized expression "(4+5)" from the start
  // of a memory operand with a missing displacement "(%ebx)" or "(,%eax)".  The
  // only way to do this without lookahead is to eat the '(' and see what is
  // after it.
  const MCExpr *Disp = MCConstantExpr::Create(0, getParser().getContext());
  if (getLexer().isNot(AsmToken::LParen)) {
    SMLoc ExprEnd;
    if (getParser().ParseExpression(Disp, ExprEnd)) return 0;

    // After parsing the base expression we could either have a parenthesized
    // memory address or not.  If not, return now.  If so, eat the (.
    if (getLexer().isNot(AsmToken::LParen)) {
      // Unless we have a segment register, treat this as an immediate.
      if (SegReg == 0)
        return X86Operand::CreateMem(Disp, MemStart, ExprEnd);
      return X86Operand::CreateMem(SegReg, Disp, 0, 0, 1, MemStart, ExprEnd);
    }

    // Eat the '('.
    Parser.Lex();
  } else {
    // Okay, we have a '('.  We don't know if this is an expression or not, but
    // so we have to eat the ( to see beyond it.
    SMLoc LParenLoc = Parser.getTok().getLoc();
    Parser.Lex(); // Eat the '('.

    if (getLexer().is(AsmToken::Percent) || getLexer().is(AsmToken::Comma)) {
      // Nothing to do here, fall into the code below with the '(' part of the
      // memory operand consumed.
    } else {
      SMLoc ExprEnd;

      // It must be an parenthesized expression, parse it now.
      if (getParser().ParseParenExpression(Disp, ExprEnd))
        return 0;

      // After parsing the base expression we could either have a parenthesized
      // memory address or not.  If not, return now.  If so, eat the (.
      if (getLexer().isNot(AsmToken::LParen)) {
        // Unless we have a segment register, treat this as an immediate.
        if (SegReg == 0)
          return X86Operand::CreateMem(Disp, LParenLoc, ExprEnd);
        return X86Operand::CreateMem(SegReg, Disp, 0, 0, 1, MemStart, ExprEnd);
      }

      // Eat the '('.
      Parser.Lex();
    }
  }

  // If we reached here, then we just ate the ( of the memory operand.  Process
  // the rest of the memory operand.
  unsigned BaseReg = 0, IndexReg = 0, Scale = 1;

  if (getLexer().is(AsmToken::Percent)) {
    SMLoc L;
    if (ParseRegister(BaseReg, L, L)) return 0;
    if (BaseReg == X86::EIZ || BaseReg == X86::RIZ) {
      Error(L, "eiz and riz can only be used as index registers");
      return 0;
    }
  }

  if (getLexer().is(AsmToken::Comma)) {
    Parser.Lex(); // Eat the comma.

    // Following the comma we should have either an index register, or a scale
    // value. We don't support the later form, but we want to parse it
    // correctly.
    //
    // Not that even though it would be completely consistent to support syntax
    // like "1(%eax,,1)", the assembler doesn't. Use "eiz" or "riz" for this.
    if (getLexer().is(AsmToken::Percent)) {
      SMLoc L;
      if (ParseRegister(IndexReg, L, L)) return 0;

      if (getLexer().isNot(AsmToken::RParen)) {
        // Parse the scale amount:
        //  ::= ',' [scale-expression]
        if (getLexer().isNot(AsmToken::Comma)) {
          Error(Parser.getTok().getLoc(),
                "expected comma in scale expression");
          return 0;
        }
        Parser.Lex(); // Eat the comma.

        if (getLexer().isNot(AsmToken::RParen)) {
          SMLoc Loc = Parser.getTok().getLoc();

          int64_t ScaleVal;
          if (getParser().ParseAbsoluteExpression(ScaleVal))
            return 0;

          // Validate the scale amount.
          if (ScaleVal != 1 && ScaleVal != 2 && ScaleVal != 4 && ScaleVal != 8){
            Error(Loc, "scale factor in address must be 1, 2, 4 or 8");
            return 0;
          }
          Scale = (unsigned)ScaleVal;
        }
      }
    } else if (getLexer().isNot(AsmToken::RParen)) {
      // A scale amount without an index is ignored.
      // index.
      SMLoc Loc = Parser.getTok().getLoc();

      int64_t Value;
      if (getParser().ParseAbsoluteExpression(Value))
        return 0;

      if (Value != 1)
        Warning(Loc, "scale factor without index register is ignored");
      Scale = 1;
    }
  }

  // Ok, we've eaten the memory operand, verify we have a ')' and eat it too.
  if (getLexer().isNot(AsmToken::RParen)) {
    Error(Parser.getTok().getLoc(), "unexpected token in memory operand");
    return 0;
  }
  SMLoc MemEnd = Parser.getTok().getLoc();
  Parser.Lex(); // Eat the ')'.

  return X86Operand::CreateMem(SegReg, Disp, BaseReg, IndexReg, Scale,
                               MemStart, MemEnd);
}

bool X86ATTAsmParser::
ParseInstruction(StringRef Name, SMLoc NameLoc,
                 SmallVectorImpl<MCParsedAsmOperand*> &Operands) {
  // FIXME: Hack to recognize "sal..." and "rep..." for now. We need a way to
  // represent alternative syntaxes in the .td file, without requiring
  // instruction duplication.
  StringRef PatchedName = StringSwitch<StringRef>(Name)
    .Case("sal", "shl")
    .Case("salb", "shlb")
    .Case("sall", "shll")
    .Case("salq", "shlq")
    .Case("salw", "shlw")
    .Case("repe", "rep")
    .Case("repz", "rep")
    .Case("repnz", "repne")
    .Case("iret", "iretl")
    .Case("sysret", "sysretl")
    .Case("cbw",  "cbtw")
    .Case("cwd",  "cwtd")
    .Case("cdq", "cltd")
    .Case("cwde", "cwtl")
    .Case("cdqe", "cltq")
    .Case("smovb", "movsb")
    .Case("smovw", "movsw")
    .Case("smovl", "movsl")
    .Case("smovq", "movsq")
    .Case("push", Is64Bit ? "pushq" : "pushl")
    .Case("pop", Is64Bit ? "popq" : "popl")
    .Case("pushf", Is64Bit ? "pushfq" : "pushfl")
    .Case("popf",  Is64Bit ? "popfq"  : "popfl")
    .Case("pushfd", "pushfl")
    .Case("popfd",  "popfl")
    .Case("retl", Is64Bit ? "retl" : "ret")
    .Case("retq", Is64Bit ? "ret" : "retq")
    .Case("setz", "sete")  .Case("setnz", "setne")
    .Case("setc", "setb")  .Case("setna", "setbe")
    .Case("setnae", "setb").Case("setnb", "setae")
    .Case("setnbe", "seta").Case("setnc", "setae")
    .Case("setng", "setle").Case("setnge", "setl")
    .Case("setnl", "setge").Case("setnle", "setg")
    .Case("setpe", "setp") .Case("setpo", "setnp")
    .Case("jz", "je")  .Case("jnz", "jne")
    .Case("jc", "jb")  .Case("jna", "jbe")
    .Case("jnae", "jb").Case("jnb", "jae")
    .Case("jnbe", "ja").Case("jnc", "jae")
    .Case("jng", "jle").Case("jnge", "jl")
    .Case("jnl", "jge").Case("jnle", "jg")
    .Case("jpe", "jp") .Case("jpo", "jnp")
    // Condition code aliases for 16-bit, 32-bit, 64-bit and unspec operands.
    .Case("cmovcw",  "cmovbw") .Case("cmovcl",  "cmovbl")
    .Case("cmovcq",  "cmovbq") .Case("cmovc",   "cmovb")
    .Case("cmovnaew","cmovbw") .Case("cmovnael","cmovbl")
    .Case("cmovnaeq","cmovbq") .Case("cmovnae", "cmovb")
    .Case("cmovnaw", "cmovbew").Case("cmovnal", "cmovbel")
    .Case("cmovnaq", "cmovbeq").Case("cmovna",  "cmovbe")
    .Case("cmovnbw", "cmovaew").Case("cmovnbl", "cmovael")
    .Case("cmovnbq", "cmovaeq").Case("cmovnb",  "cmovae")
    .Case("cmovnbew","cmovaw") .Case("cmovnbel","cmoval")
    .Case("cmovnbeq","cmovaq") .Case("cmovnbe", "cmova")
    .Case("cmovncw", "cmovaew").Case("cmovncl", "cmovael")
    .Case("cmovncq", "cmovaeq").Case("cmovnc",  "cmovae")
    .Case("cmovngw", "cmovlew").Case("cmovngl", "cmovlel")
    .Case("cmovngq", "cmovleq").Case("cmovng",  "cmovle")
    .Case("cmovnw",  "cmovgew").Case("cmovnl",  "cmovgel")
    .Case("cmovnq",  "cmovgeq").Case("cmovn",   "cmovge")
    .Case("cmovngw", "cmovlew").Case("cmovngl", "cmovlel")
    .Case("cmovngq", "cmovleq").Case("cmovng",  "cmovle")
    .Case("cmovngew","cmovlw") .Case("cmovngel","cmovll")
    .Case("cmovngeq","cmovlq") .Case("cmovnge", "cmovl")
    .Case("cmovnlw", "cmovgew").Case("cmovnll", "cmovgel")
    .Case("cmovnlq", "cmovgeq").Case("cmovnl",  "cmovge")
    .Case("cmovnlew","cmovgw") .Case("cmovnlel","cmovgl")
    .Case("cmovnleq","cmovgq") .Case("cmovnle", "cmovg")
    .Case("cmovnzw", "cmovnew").Case("cmovnzl", "cmovnel")
    .Case("cmovnzq", "cmovneq").Case("cmovnz",  "cmovne")
    .Case("cmovzw",  "cmovew") .Case("cmovzl",  "cmovel")
    .Case("cmovzq",  "cmoveq") .Case("cmovz",   "cmove")
    // Floating point stack cmov aliases.
    .Case("fcmovz", "fcmove")
    .Case("fcmova", "fcmovnbe")
    .Case("fcmovnae", "fcmovb")
    .Case("fcmovna", "fcmovbe")
    .Case("fcmovae", "fcmovnb")
    .Case("fwait", "wait")
    .Case("movzx", "movzb")  // FIXME: Not correct.
    .Case("fildq", "fildll")
    .Default(Name);

  // FIXME: Hack to recognize cmp<comparison code>{ss,sd,ps,pd}.
  const MCExpr *ExtraImmOp = 0;
  if ((PatchedName.startswith("cmp") || PatchedName.startswith("vcmp")) &&
      (PatchedName.endswith("ss") || PatchedName.endswith("sd") ||
       PatchedName.endswith("ps") || PatchedName.endswith("pd"))) {
    bool IsVCMP = PatchedName.startswith("vcmp");
    unsigned SSECCIdx = IsVCMP ? 4 : 3;
    unsigned SSEComparisonCode = StringSwitch<unsigned>(
      PatchedName.slice(SSECCIdx, PatchedName.size() - 2))
      .Case("eq",          0)
      .Case("lt",          1)
      .Case("le",          2)
      .Case("unord",       3)
      .Case("neq",         4)
      .Case("nlt",         5)
      .Case("nle",         6)
      .Case("ord",         7)
      .Case("eq_uq",       8)
      .Case("nge",         9)
      .Case("ngt",      0x0A)
      .Case("false",    0x0B)
      .Case("neq_oq",   0x0C)
      .Case("ge",       0x0D)
      .Case("gt",       0x0E)
      .Case("true",     0x0F)
      .Case("eq_os",    0x10)
      .Case("lt_oq",    0x11)
      .Case("le_oq",    0x12)
      .Case("unord_s",  0x13)
      .Case("neq_us",   0x14)
      .Case("nlt_uq",   0x15)
      .Case("nle_uq",   0x16)
      .Case("ord_s",    0x17)
      .Case("eq_us",    0x18)
      .Case("nge_uq",   0x19)
      .Case("ngt_uq",   0x1A)
      .Case("false_os", 0x1B)
      .Case("neq_os",   0x1C)
      .Case("ge_oq",    0x1D)
      .Case("gt_oq",    0x1E)
      .Case("true_us",  0x1F)
      .Default(~0U);
    if (SSEComparisonCode != ~0U) {
      ExtraImmOp = MCConstantExpr::Create(SSEComparisonCode,
                                          getParser().getContext());
      if (PatchedName.endswith("ss")) {
        PatchedName = IsVCMP ? "vcmpss" : "cmpss";
      } else if (PatchedName.endswith("sd")) {
        PatchedName = IsVCMP ? "vcmpsd" : "cmpsd";
      } else if (PatchedName.endswith("ps")) {
        PatchedName = IsVCMP ? "vcmpps" : "cmpps";
      } else {
        assert(PatchedName.endswith("pd") && "Unexpected mnemonic!");
        PatchedName = IsVCMP ? "vcmppd" : "cmppd";
      }
    }
  }

  // FIXME: Hack to recognize vpclmul<src1_quadword, src2_quadword>dq
  if (PatchedName.startswith("vpclmul")) {
    unsigned CLMULQuadWordSelect = StringSwitch<unsigned>(
      PatchedName.slice(7, PatchedName.size() - 2))
      .Case("lqlq", 0x00) // src1[63:0],   src2[63:0]
      .Case("hqlq", 0x01) // src1[127:64], src2[63:0]
      .Case("lqhq", 0x10) // src1[63:0],   src2[127:64]
      .Case("hqhq", 0x11) // src1[127:64], src2[127:64]
      .Default(~0U);
    if (CLMULQuadWordSelect != ~0U) {
      ExtraImmOp = MCConstantExpr::Create(CLMULQuadWordSelect,
                                          getParser().getContext());
      assert(PatchedName.endswith("dq") && "Unexpected mnemonic!");
      PatchedName = "vpclmulqdq";
    }
  }

  Operands.push_back(X86Operand::CreateToken(PatchedName, NameLoc));

  if (ExtraImmOp)
    Operands.push_back(X86Operand::CreateImm(ExtraImmOp, NameLoc, NameLoc));


  // Determine whether this is an instruction prefix.
  bool isPrefix =
    PatchedName == "lock" || PatchedName == "rep" ||
    PatchedName == "repne";


  // This does the actual operand parsing.  Don't parse any more if we have a
  // prefix juxtaposed with an operation like "lock incl 4(%rax)", because we
  // just want to parse the "lock" as the first instruction and the "incl" as
  // the next one.
  if (getLexer().isNot(AsmToken::EndOfStatement) && !isPrefix) {

    // Parse '*' modifier.
    if (getLexer().is(AsmToken::Star)) {
      SMLoc Loc = Parser.getTok().getLoc();
      Operands.push_back(X86Operand::CreateToken("*", Loc));
      Parser.Lex(); // Eat the star.
    }

    // Read the first operand.
    if (X86Operand *Op = ParseOperand())
      Operands.push_back(Op);
    else {
      Parser.EatToEndOfStatement();
      return true;
    }

    while (getLexer().is(AsmToken::Comma)) {
      Parser.Lex();  // Eat the comma.

      // Parse and remember the operand.
      if (X86Operand *Op = ParseOperand())
        Operands.push_back(Op);
      else {
        Parser.EatToEndOfStatement();
        return true;
      }
    }

    if (getLexer().isNot(AsmToken::EndOfStatement)) {
      Parser.EatToEndOfStatement();
      return TokError("unexpected token in argument list");
    }
  }

  if (getLexer().is(AsmToken::EndOfStatement))
    Parser.Lex(); // Consume the EndOfStatement

  // Hack to allow 'movq <largeimm>, <reg>' as an alias for movabsq.
  if ((Name == "movq" || Name == "mov") && Operands.size() == 3 &&
      static_cast<X86Operand*>(Operands[2])->isReg() &&
      static_cast<X86Operand*>(Operands[1])->isImm() &&
      !static_cast<X86Operand*>(Operands[1])->isImmSExti64i32()) {
    delete Operands[0];
    Operands[0] = X86Operand::CreateToken("movabsq", NameLoc);
  }

  // FIXME: Hack to handle recognize s{hr,ar,hl} $1, <op>.  Canonicalize to
  // "shift <op>".
  if ((Name.startswith("shr") || Name.startswith("sar") ||
       Name.startswith("shl")) &&
      Operands.size() == 3) {
    X86Operand *Op1 = static_cast<X86Operand*>(Operands[1]);
    if (Op1->isImm() && isa<MCConstantExpr>(Op1->getImm()) &&
        cast<MCConstantExpr>(Op1->getImm())->getValue() == 1) {
      delete Operands[1];
      Operands.erase(Operands.begin() + 1);
    }
  }

  // FIXME: Hack to handle recognize "rc[lr] <op>" -> "rcl $1, <op>".
  if ((Name.startswith("rcl") || Name.startswith("rcr")) &&
      Operands.size() == 2) {
    const MCExpr *One = MCConstantExpr::Create(1, getParser().getContext());
    Operands.push_back(X86Operand::CreateImm(One, NameLoc, NameLoc));
    std::swap(Operands[1], Operands[2]);
  }

  // FIXME: Hack to handle recognize "sh[lr]d op,op" -> "shld $1, op,op".
  if ((Name.startswith("shld") || Name.startswith("shrd")) &&
      Operands.size() == 3) {
    const MCExpr *One = MCConstantExpr::Create(1, getParser().getContext());
    Operands.insert(Operands.begin()+1,
                    X86Operand::CreateImm(One, NameLoc, NameLoc));
  }


  // FIXME: Hack to handle recognize "in[bwl] <op>".  Canonicalize it to
  // "inb <op>, %al".
  if ((Name == "inb" || Name == "inw" || Name == "inl") &&
      Operands.size() == 2) {
    unsigned Reg;
    if (Name[2] == 'b')
      Reg = MatchRegisterName("al");
    else if (Name[2] == 'w')
      Reg = MatchRegisterName("ax");
    else
      Reg = MatchRegisterName("eax");
    SMLoc Loc = Operands.back()->getEndLoc();
    Operands.push_back(X86Operand::CreateReg(Reg, Loc, Loc));
  }

  // FIXME: Hack to handle recognize "out[bwl] <op>".  Canonicalize it to
  // "outb %al, <op>".
  if ((Name == "outb" || Name == "outw" || Name == "outl") &&
      Operands.size() == 2) {
    unsigned Reg;
    if (Name[3] == 'b')
      Reg = MatchRegisterName("al");
    else if (Name[3] == 'w')
      Reg = MatchRegisterName("ax");
    else
      Reg = MatchRegisterName("eax");
    SMLoc Loc = Operands.back()->getEndLoc();
    Operands.push_back(X86Operand::CreateReg(Reg, Loc, Loc));
    std::swap(Operands[1], Operands[2]);
  }

  // FIXME: Hack to handle "out[bwl]? %al, (%dx)" -> "outb %al, %dx".
  if ((Name == "outb" || Name == "outw" || Name == "outl" || Name == "out") &&
      Operands.size() == 3) {
    X86Operand &Op = *(X86Operand*)Operands.back();
    if (Op.isMem() && Op.Mem.SegReg == 0 &&
        isa<MCConstantExpr>(Op.Mem.Disp) &&
        cast<MCConstantExpr>(Op.Mem.Disp)->getValue() == 0 &&
        Op.Mem.BaseReg == MatchRegisterName("dx") && Op.Mem.IndexReg == 0) {
      SMLoc Loc = Op.getEndLoc();
      Operands.back() = X86Operand::CreateReg(Op.Mem.BaseReg, Loc, Loc);
      delete &Op;
    }
  }

  // FIXME: Hack to handle "f{mul*,add*,sub*,div*} $op, st(0)" the same as
  // "f{mul*,add*,sub*,div*} $op"
  if ((Name.startswith("fmul") || Name.startswith("fadd") ||
       Name.startswith("fsub") || Name.startswith("fdiv")) &&
      Operands.size() == 3 &&
      static_cast<X86Operand*>(Operands[2])->isReg() &&
      static_cast<X86Operand*>(Operands[2])->getReg() == X86::ST0) {
    delete Operands[2];
    Operands.erase(Operands.begin() + 2);
  }

  // FIXME: Hack to handle "f{mulp,addp} st(0), $op" the same as
  // "f{mulp,addp} $op", since they commute.  We also allow fdivrp/fsubrp even
  // though they don't commute, solely because gas does support this.
  if ((Name=="fmulp" || Name=="faddp" || Name=="fsubrp" || Name=="fdivrp") &&
      Operands.size() == 3 &&
      static_cast<X86Operand*>(Operands[1])->isReg() &&
      static_cast<X86Operand*>(Operands[1])->getReg() == X86::ST0) {
    delete Operands[1];
    Operands.erase(Operands.begin() + 1);
  }

  // FIXME: Hack to handle "imul <imm>, B" which is an alias for "imul <imm>, B,
  // B".
  if (Name.startswith("imul") && Operands.size() == 3 &&
      static_cast<X86Operand*>(Operands[1])->isImm() &&
      static_cast<X86Operand*>(Operands.back())->isReg()) {
    X86Operand *Op = static_cast<X86Operand*>(Operands.back());
    Operands.push_back(X86Operand::CreateReg(Op->getReg(), Op->getStartLoc(),
                                             Op->getEndLoc()));
  }

  // 'sldt <mem>' can be encoded with either sldtw or sldtq with the same
  // effect (both store to a 16-bit mem).  Force to sldtw to avoid ambiguity
  // errors, since its encoding is the most compact.
  if (Name == "sldt" && Operands.size() == 2 &&
      static_cast<X86Operand*>(Operands[1])->isMem()) {
    delete Operands[0];
    Operands[0] = X86Operand::CreateToken("sldtw", NameLoc);
  }

  // The assembler accepts "xchgX <reg>, <mem>" and "xchgX <mem>, <reg>" as
  // synonyms.  Our tables only have the "<reg>, <mem>" form, so if we see the
  // other operand order, swap them.
  if (Name == "xchgb" || Name == "xchgw" || Name == "xchgl" || Name == "xchgq"||
      Name == "xchg")
    if (Operands.size() == 3 &&
        static_cast<X86Operand*>(Operands[1])->isMem() &&
        static_cast<X86Operand*>(Operands[2])->isReg()) {
      std::swap(Operands[1], Operands[2]);
    }

  // The assembler accepts "testX <reg>, <mem>" and "testX <mem>, <reg>" as
  // synonyms.  Our tables only have the "<mem>, <reg>" form, so if we see the
  // other operand order, swap them.
  if (Name == "testb" || Name == "testw" || Name == "testl" || Name == "testq"||
      Name == "test")
    if (Operands.size() == 3 &&
        static_cast<X86Operand*>(Operands[1])->isReg() &&
        static_cast<X86Operand*>(Operands[2])->isMem()) {
      std::swap(Operands[1], Operands[2]);
    }

  // The assembler accepts these instructions with no operand as a synonym for
  // an instruction acting on st(1).  e.g. "fxch" -> "fxch %st(1)".
  if ((Name == "fxch" || Name == "fucom" || Name == "fucomp" ||
       Name == "faddp" || Name == "fsubp" || Name == "fsubrp" ||
       Name == "fmulp" || Name == "fdivp" || Name == "fdivrp") &&
      Operands.size() == 1) {
    Operands.push_back(X86Operand::CreateReg(MatchRegisterName("st(1)"),
                                             NameLoc, NameLoc));
  }

  // The assembler accepts these instructions with two few operands as a synonym
  // for taking %st(1),%st(0) or X, %st(0).
  if ((Name == "fcomi" || Name == "fucomi") && Operands.size() < 3) {
    if (Operands.size() == 1)
      Operands.push_back(X86Operand::CreateReg(MatchRegisterName("st(1)"),
                                               NameLoc, NameLoc));
    Operands.push_back(X86Operand::CreateReg(MatchRegisterName("st(0)"),
                                             NameLoc, NameLoc));
  }

  // The assembler accepts various amounts of brokenness for fnstsw.
  if (Name == "fnstsw") {
    if (Operands.size() == 2 &&
        static_cast<X86Operand*>(Operands[1])->isReg()) {
      // "fnstsw al" and "fnstsw eax" -> "fnstw"
      unsigned Reg = static_cast<X86Operand*>(Operands[1])->Reg.RegNo;
      if (Reg == MatchRegisterName("eax") ||
          Reg == MatchRegisterName("al")) {
        delete Operands[1];
        Operands.pop_back();
      }
    }

    // "fnstw" -> "fnstw %ax"
    if (Operands.size() == 1)
      Operands.push_back(X86Operand::CreateReg(MatchRegisterName("ax"),
                                               NameLoc, NameLoc));
  }

  // jmp $42,$5 -> ljmp, similarly for call.
  if ((Name.startswith("call") || Name.startswith("jmp")) &&
      Operands.size() == 3 &&
      static_cast<X86Operand*>(Operands[1])->isImm() &&
      static_cast<X86Operand*>(Operands[2])->isImm()) {
    const char *NewOpName = StringSwitch<const char *>(Name)
      .Case("jmp", "ljmp")
      .Case("jmpw", "ljmpw")
      .Case("jmpl", "ljmpl")
      .Case("jmpq", "ljmpq")
      .Case("call", "lcall")
      .Case("callw", "lcallw")
      .Case("calll", "lcalll")
      .Case("callq", "lcallq")
    .Default(0);
    if (NewOpName) {
      delete Operands[0];
      Operands[0] = X86Operand::CreateToken(NewOpName, NameLoc);
      Name = NewOpName;
    }
  }

  // lcall  and ljmp  -> lcalll and ljmpl
  if ((Name == "lcall" || Name == "ljmp") && Operands.size() == 3) {
    delete Operands[0];
    Operands[0] = X86Operand::CreateToken(Name == "lcall" ? "lcalll" : "ljmpl",
                                          NameLoc);
  }

  // call foo is not ambiguous with callw.
  if (Name == "call" && Operands.size() == 2) {
    const char *NewName = Is64Bit ? "callq" : "calll";
    delete Operands[0];
    Operands[0] = X86Operand::CreateToken(NewName, NameLoc);
    Name = NewName;
  }

  // movsd -> movsl (when no operands are specified).
  if (Name == "movsd" && Operands.size() == 1) {
    delete Operands[0];
    Operands[0] = X86Operand::CreateToken("movsl", NameLoc);
  }

  // fstp <mem> -> fstps <mem>.  Without this, we'll default to fstpl due to
  // suffix searching.
  if (Name == "fstp" && Operands.size() == 2 &&
      static_cast<X86Operand*>(Operands[1])->isMem()) {
    delete Operands[0];
    Operands[0] = X86Operand::CreateToken("fstps", NameLoc);
  }


  // "clr <reg>" -> "xor <reg>, <reg>".
  if ((Name == "clrb" || Name == "clrw" || Name == "clrl" || Name == "clrq" ||
       Name == "clr") && Operands.size() == 2 &&
      static_cast<X86Operand*>(Operands[1])->isReg()) {
    unsigned RegNo = static_cast<X86Operand*>(Operands[1])->getReg();
    Operands.push_back(X86Operand::CreateReg(RegNo, NameLoc, NameLoc));
    delete Operands[0];
    Operands[0] = X86Operand::CreateToken("xor", NameLoc);
  }

  return false;
}

bool X86ATTAsmParser::ParseDirective(AsmToken DirectiveID) {
  StringRef IDVal = DirectiveID.getIdentifier();
  if (IDVal == ".word")
    return ParseDirectiveWord(2, DirectiveID.getLoc());
  return true;
}

/// ParseDirectiveWord
///  ::= .word [ expression (, expression)* ]
bool X86ATTAsmParser::ParseDirectiveWord(unsigned Size, SMLoc L) {
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    for (;;) {
      const MCExpr *Value;
      if (getParser().ParseExpression(Value))
        return true;

      getParser().getStreamer().EmitValue(Value, Size, 0 /*addrspace*/);

      if (getLexer().is(AsmToken::EndOfStatement))
        break;

      // FIXME: Improve diagnostic.
      if (getLexer().isNot(AsmToken::Comma))
        return Error(L, "unexpected token in directive");
      Parser.Lex();
    }
  }

  Parser.Lex();
  return false;
}


bool X86ATTAsmParser::
MatchAndEmitInstruction(SMLoc IDLoc,
                        SmallVectorImpl<MCParsedAsmOperand*> &Operands,
                        MCStreamer &Out) {
  assert(!Operands.empty() && "Unexpect empty operand list!");
  X86Operand *Op = static_cast<X86Operand*>(Operands[0]);
  assert(Op->isToken() && "Leading operand should always be a mnemonic!");

  // First, handle aliases that expand to multiple instructions.
  // FIXME: This should be replaced with a real .td file alias mechanism.
  if (Op->getToken() == "fstsw" || Op->getToken() == "fstcw" ||
      Op->getToken() == "finit" || Op->getToken() == "fsave" ||
      Op->getToken() == "fstenv") {
    MCInst Inst;
    Inst.setOpcode(X86::WAIT);
    Out.EmitInstruction(Inst);

    const char *Repl =
      StringSwitch<const char*>(Op->getToken())
        .Case("finit", "fninit")
        .Case("fsave", "fnsave")
        .Case("fstcw", "fnstcw")
        .Case("fstenv", "fnstenv")
        .Case("fstsw", "fnstsw")
        .Default(0);
    assert(Repl && "Unknown wait-prefixed instruction");
    delete Operands[0];
    Operands[0] = X86Operand::CreateToken(Repl, IDLoc);
  }

  bool WasOriginallyInvalidOperand = false;
  unsigned OrigErrorInfo;
  MCInst Inst;

  // First, try a direct match.
  switch (MatchInstructionImpl(Operands, Inst, OrigErrorInfo)) {
  case Match_Success:
    Out.EmitInstruction(Inst);
    return false;
  case Match_MissingFeature:
    Error(IDLoc, "instruction requires a CPU feature not currently enabled");
    return true;
  case Match_InvalidOperand:
    WasOriginallyInvalidOperand = true;
    break;
  case Match_MnemonicFail:
    break;
  }

  // FIXME: Ideally, we would only attempt suffix matches for things which are
  // valid prefixes, and we could just infer the right unambiguous
  // type. However, that requires substantially more matcher support than the
  // following hack.

  // Change the operand to point to a temporary token.
  StringRef Base = Op->getToken();
  SmallString<16> Tmp;
  Tmp += Base;
  Tmp += ' ';
  Op->setTokenValue(Tmp.str());

  // Check for the various suffix matches.
  Tmp[Base.size()] = 'b';
  unsigned BErrorInfo, WErrorInfo, LErrorInfo, QErrorInfo;
  MatchResultTy MatchB = MatchInstructionImpl(Operands, Inst, BErrorInfo);
  Tmp[Base.size()] = 'w';
  MatchResultTy MatchW = MatchInstructionImpl(Operands, Inst, WErrorInfo);
  Tmp[Base.size()] = 'l';
  MatchResultTy MatchL = MatchInstructionImpl(Operands, Inst, LErrorInfo);
  Tmp[Base.size()] = 'q';
  MatchResultTy MatchQ = MatchInstructionImpl(Operands, Inst, QErrorInfo);

  // Restore the old token.
  Op->setTokenValue(Base);

  // If exactly one matched, then we treat that as a successful match (and the
  // instruction will already have been filled in correctly, since the failing
  // matches won't have modified it).
  unsigned NumSuccessfulMatches =
    (MatchB == Match_Success) + (MatchW == Match_Success) +
    (MatchL == Match_Success) + (MatchQ == Match_Success);
  if (NumSuccessfulMatches == 1) {
    Out.EmitInstruction(Inst);
    return false;
  }

  // Otherwise, the match failed, try to produce a decent error message.

  // If we had multiple suffix matches, then identify this as an ambiguous
  // match.
  if (NumSuccessfulMatches > 1) {
    char MatchChars[4];
    unsigned NumMatches = 0;
    if (MatchB == Match_Success)
      MatchChars[NumMatches++] = 'b';
    if (MatchW == Match_Success)
      MatchChars[NumMatches++] = 'w';
    if (MatchL == Match_Success)
      MatchChars[NumMatches++] = 'l';
    if (MatchQ == Match_Success)
      MatchChars[NumMatches++] = 'q';

    SmallString<126> Msg;
    raw_svector_ostream OS(Msg);
    OS << "ambiguous instructions require an explicit suffix (could be ";
    for (unsigned i = 0; i != NumMatches; ++i) {
      if (i != 0)
        OS << ", ";
      if (i + 1 == NumMatches)
        OS << "or ";
      OS << "'" << Base << MatchChars[i] << "'";
    }
    OS << ")";
    Error(IDLoc, OS.str());
    return true;
  }

  // Okay, we know that none of the variants matched successfully.

  // If all of the instructions reported an invalid mnemonic, then the original
  // mnemonic was invalid.
  if ((MatchB == Match_MnemonicFail) && (MatchW == Match_MnemonicFail) &&
      (MatchL == Match_MnemonicFail) && (MatchQ == Match_MnemonicFail)) {
    if (!WasOriginallyInvalidOperand) {
      Error(IDLoc, "invalid instruction mnemonic '" + Base + "'");
      return true;
    }

    // Recover location info for the operand if we know which was the problem.
    SMLoc ErrorLoc = IDLoc;
    if (OrigErrorInfo != ~0U) {
      if (OrigErrorInfo >= Operands.size())
        return Error(IDLoc, "too few operands for instruction");

      ErrorLoc = ((X86Operand*)Operands[OrigErrorInfo])->getStartLoc();
      if (ErrorLoc == SMLoc()) ErrorLoc = IDLoc;
    }

    return Error(ErrorLoc, "invalid operand for instruction");
  }

  // If one instruction matched with a missing feature, report this as a
  // missing feature.
  if ((MatchB == Match_MissingFeature) + (MatchW == Match_MissingFeature) +
      (MatchL == Match_MissingFeature) + (MatchQ == Match_MissingFeature) == 1){
    Error(IDLoc, "instruction requires a CPU feature not currently enabled");
    return true;
  }

  // If one instruction matched with an invalid operand, report this as an
  // operand failure.
  if ((MatchB == Match_InvalidOperand) + (MatchW == Match_InvalidOperand) +
      (MatchL == Match_InvalidOperand) + (MatchQ == Match_InvalidOperand) == 1){
    Error(IDLoc, "invalid operand for instruction");
    return true;
  }

  // If all of these were an outright failure, report it in a useless way.
  // FIXME: We should give nicer diagnostics about the exact failure.
  Error(IDLoc, "unknown use of instruction mnemonic without a size suffix");
  return true;
}


extern "C" void LLVMInitializeX86AsmLexer();

// Force static initialization.
extern "C" void LLVMInitializeX86AsmParser() {
  RegisterAsmParser<X86_32ATTAsmParser> X(TheX86_32Target);
  RegisterAsmParser<X86_64ATTAsmParser> Y(TheX86_64Target);
  LLVMInitializeX86AsmLexer();
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "X86GenAsmMatcher.inc"
