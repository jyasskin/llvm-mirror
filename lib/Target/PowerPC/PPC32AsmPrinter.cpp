//===-- PPC32AsmPrinter.cpp - Print machine instrs to PowerPC assembly ----===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to PowerPC assembly language. This printer is
// the output mechanism used by `llc'.
//
// Documentation at http://developer.apple.com/documentation/DeveloperTools/
// Reference/Assembler/ASMIntroduction/chapter_1_section_1.html
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "asmprinter"
#include "PowerPC.h"
#include "PPC32TargetMachine.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Module.h"
#include "llvm/Assembly/Writer.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/Support/Mangler.h"
#include "Support/CommandLine.h"
#include "Support/Debug.h"
#include "Support/Statistic.h"
#include "Support/StringExtras.h"
#include <set>
using namespace llvm;

namespace {
  Statistic<> EmittedInsts("asm-printer", "Number of machine instrs printed");

  struct PPC32AsmPrinter : public AsmPrinter {
    std::set<std::string> FnStubs, GVStubs, LinkOnceStubs;
    std::set<std::string> Strings;

    PPC32AsmPrinter(std::ostream &O, TargetMachine &TM)
      : AsmPrinter(O, TM), LabelNumber(0) {
      CommentString = ";";
      GlobalPrefix = "_";
      ZeroDirective = "\t.space\t";  // ".space N" emits N zeros.
      Data64bitsDirective = 0;       // we can't emit a 64-bit unit
      AlignmentIsInBytes = false;    // Alignment is by power of 2.
    }

    /// Unique incrementer for label values for referencing Global values.
    ///
    unsigned LabelNumber;
  
    virtual const char *getPassName() const {
      return "PPC32 Assembly Printer";
    }

    PPC32TargetMachine &getTM() {
      return static_cast<PPC32TargetMachine&>(TM);
    }

    /// printInstruction - This method is automatically generated by tablegen
    /// from the instruction set description.  This method returns true if the
    /// machine instruction was sufficiently described to print it, otherwise it
    /// returns false.
    bool printInstruction(const MachineInstr *MI);

    void printMachineInstruction(const MachineInstr *MI);
    void printOp(const MachineOperand &MO, bool LoadAddrOp = false);
    void printImmOp(const MachineOperand &MO, unsigned ArgType);

    void printOperand(const MachineInstr *MI, unsigned OpNo, MVT::ValueType VT){
      const MachineOperand &MO = MI->getOperand(OpNo);
      if (MO.getType() == MachineOperand::MO_MachineRegister) {
        assert(MRegisterInfo::isPhysicalRegister(MO.getReg())&&"Not physreg??");
        O << LowercaseString(TM.getRegisterInfo()->get(MO.getReg()).Name);
      } else if (MO.isImmediate()) {
        O << MO.getImmedValue();
      } else {
        printOp(MO);
      }
    }

    void printU16ImmOperand(const MachineInstr *MI, unsigned OpNo,
                            MVT::ValueType VT) {
      O << (unsigned short)MI->getOperand(OpNo).getImmedValue();
    }

    void printConstantPool(MachineConstantPool *MCP);
    bool runOnMachineFunction(MachineFunction &F);    
    bool doFinalization(Module &M);
  };
} // end of anonymous namespace

/// createPPC32AsmPrinterPass - Returns a pass that prints the PPC
/// assembly code for a MachineFunction to the given output stream,
/// using the given target machine description.  This should work
/// regardless of whether the function is in SSA form or not.
///
FunctionPass *llvm::createPPC32AsmPrinter(std::ostream &o, TargetMachine &tm) {
  return new PPC32AsmPrinter(o, tm);
}

// Include the auto-generated portion of the assembly writer
#include "PowerPCGenAsmWriter.inc"

/// printConstantPool - Print to the current output stream assembly
/// representations of the constants in the constant pool MCP. This is
/// used to print out constants which have been "spilled to memory" by
/// the code generator.
///
void PPC32AsmPrinter::printConstantPool(MachineConstantPool *MCP) {
  const std::vector<Constant*> &CP = MCP->getConstants();
  const TargetData &TD = TM.getTargetData();
 
  if (CP.empty()) return;

  for (unsigned i = 0, e = CP.size(); i != e; ++i) {
    O << "\t.const\n";
    emitAlignment(TD.getTypeAlignmentShift(CP[i]->getType()));
    O << ".CPI" << CurrentFnName << "_" << i << ":\t\t\t\t\t" << CommentString
      << *CP[i] << "\n";
    emitGlobalConstant(CP[i]);
  }
}

/// runOnMachineFunction - This uses the printMachineInstruction()
/// method to print assembly for each instruction.
///
bool PPC32AsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  setupMachineFunction(MF);
  O << "\n\n";

  // Print out constants referenced by the function
  printConstantPool(MF.getConstantPool());

  // Print out labels for the function.
  O << "\t.text\n";
  emitAlignment(2);
  O << "\t.globl\t" << CurrentFnName << "\n";
  O << CurrentFnName << ":\n";

  // Print out code for the function.
  for (MachineFunction::const_iterator I = MF.begin(), E = MF.end();
       I != E; ++I) {
    // Print a label for the basic block.
    O << ".LBB" << CurrentFnName << "_" << I->getNumber() << ":\t"
      << CommentString << " " << I->getBasicBlock()->getName() << "\n";
    for (MachineBasicBlock::const_iterator II = I->begin(), E = I->end();
         II != E; ++II) {
      // Print the assembly for the instruction.
      O << "\t";
      printMachineInstruction(II);
    }
  }
  ++LabelNumber;

  // We didn't modify anything.
  return false;
}

void PPC32AsmPrinter::printOp(const MachineOperand &MO,
                              bool LoadAddrOp /* = false */) {
  const MRegisterInfo &RI = *TM.getRegisterInfo();
  int new_symbol;
  
  switch (MO.getType()) {
  case MachineOperand::MO_VirtualRegister:
    if (Value *V = MO.getVRegValueOrNull()) {
      O << "<" << V->getName() << ">";
      return;
    }
    // FALLTHROUGH
  case MachineOperand::MO_MachineRegister:
  case MachineOperand::MO_CCRegister:
    O << LowercaseString(RI.get(MO.getReg()).Name);
    return;

  case MachineOperand::MO_SignExtendedImmed:
  case MachineOperand::MO_UnextendedImmed:
    std::cerr << "printOp() does not handle immediate values\n";
    abort();
    return;

  case MachineOperand::MO_PCRelativeDisp:
    std::cerr << "Shouldn't use addPCDisp() when building PPC MachineInstrs";
    abort();
    return;
    
  case MachineOperand::MO_MachineBasicBlock: {
    MachineBasicBlock *MBBOp = MO.getMachineBasicBlock();
    O << ".LBB" << Mang->getValueName(MBBOp->getParent()->getFunction())
      << "_" << MBBOp->getNumber() << "\t; "
      << MBBOp->getBasicBlock()->getName();
    return;
  }

  case MachineOperand::MO_ConstantPoolIndex:
    O << ".CPI" << CurrentFnName << "_" << MO.getConstantPoolIndex();
    return;

  case MachineOperand::MO_ExternalSymbol:
    O << MO.getSymbolName();
    return;

  case MachineOperand::MO_GlobalAddress: {
    GlobalValue *GV = MO.getGlobal();
    std::string Name = Mang->getValueName(GV);

    // Dynamically-resolved functions need a stub for the function.  Be
    // wary however not to output $stub for external functions whose addresses
    // are taken.  Those should be emitted as $non_lazy_ptr below.
    Function *F = dyn_cast<Function>(GV);
    if (F && F->isExternal() && !LoadAddrOp &&
        getTM().CalledFunctions.count(F)) {
      FnStubs.insert(Name);
      O << "L" << Name << "$stub";
      return;
    }
    
    // External global variables need a non-lazily-resolved stub
    if (GV->isExternal() && getTM().AddressTaken.count(GV)) {
      GVStubs.insert(Name);
      O << "L" << Name << "$non_lazy_ptr";
      return;
    }
    
    if (F && LoadAddrOp && getTM().AddressTaken.count(GV)) {
      LinkOnceStubs.insert(Name);
      O << "L" << Name << "$non_lazy_ptr";
      return;
    }
            
    O << Mang->getValueName(GV);
    return;
  }
    
  default:
    O << "<unknown operand type: " << MO.getType() << ">";
    return;
  }
}

void PPC32AsmPrinter::printImmOp(const MachineOperand &MO, unsigned ArgType) {
  int Imm = MO.getImmedValue();
  if (ArgType == PPCII::Simm16 || ArgType == PPCII::Disimm16) {
    O << (short)Imm;
  } else {
    O << Imm;
  }
}

/// printMachineInstruction -- Print out a single PowerPC MI in Darwin syntax to
/// the current output stream.
///
void PPC32AsmPrinter::printMachineInstruction(const MachineInstr *MI) {
  ++EmittedInsts;
  if (printInstruction(MI))
    return; // Printer was automatically generated
    
  unsigned Opcode = MI->getOpcode();
  const TargetInstrInfo &TII = *TM.getInstrInfo();
  const TargetInstrDescriptor &Desc = TII.get(Opcode);
  unsigned i;

  unsigned ArgCount = MI->getNumOperands();
  unsigned ArgType[] = {
    (Desc.TSFlags >> PPCII::Arg0TypeShift) & PPCII::ArgTypeMask,
    (Desc.TSFlags >> PPCII::Arg1TypeShift) & PPCII::ArgTypeMask,
    (Desc.TSFlags >> PPCII::Arg2TypeShift) & PPCII::ArgTypeMask,
    (Desc.TSFlags >> PPCII::Arg3TypeShift) & PPCII::ArgTypeMask,
    (Desc.TSFlags >> PPCII::Arg4TypeShift) & PPCII::ArgTypeMask
  };
  assert(((Desc.TSFlags & PPCII::VMX) == 0) &&
         "Instruction requires VMX support");
  assert(((Desc.TSFlags & PPCII::PPC64) == 0) &&
         "Instruction requires 64 bit support");

  // CALLpcrel and CALLindirect are handled specially here to print only the
  // appropriate number of args that the assembler expects.  This is because
  // may have many arguments appended to record the uses of registers that are
  // holding arguments to the called function.
  if (Opcode == PPC::COND_BRANCH) {
    std::cerr << "Error: untranslated conditional branch psuedo instruction!\n";
    abort();
  } else if (Opcode == PPC::IMPLICIT_DEF) {
    --EmittedInsts; // Not an actual machine instruction
    O << "; IMPLICIT DEF ";
    printOp(MI->getOperand(0));
    O << "\n";
    return;
  } else if (Opcode == PPC::CALLpcrel) {
    O << TII.getName(Opcode) << " ";
    printOp(MI->getOperand(0));
    O << "\n";
    return;
  } else if (Opcode == PPC::CALLindirect) {
    O << TII.getName(Opcode) << " ";
    printImmOp(MI->getOperand(0), ArgType[0]);
    O << ", ";
    printImmOp(MI->getOperand(1), ArgType[0]);
    O << "\n";
    return;
  } else if (Opcode == PPC::MovePCtoLR) {
    ++EmittedInsts; // Actually two machine instructions
    // FIXME: should probably be converted to cout.width and cout.fill
    O << "bl \"L0000" << LabelNumber << "$pb\"\n";
    O << "\"L0000" << LabelNumber << "$pb\":\n";
    O << "\tmflr ";
    printOp(MI->getOperand(0));
    O << "\n";
    return;
  }

  O << TII.getName(Opcode) << " ";
  if (Opcode == PPC::LOADHiAddr) {
    printOp(MI->getOperand(0));
    O << ", ";
    if (MI->getOperand(1).getReg() == PPC::R0)
      O << "0";
    else
      printOp(MI->getOperand(1));
    O << ", ha16(" ;
    printOp(MI->getOperand(2), true /* LoadAddrOp */);
     O << "-\"L0000" << LabelNumber << "$pb\")\n";
  } else if (ArgCount == 3 && (MI->getOperand(2).isConstantPoolIndex() 
                            || MI->getOperand(2).isGlobalAddress())) {
    printOp(MI->getOperand(0));
    O << ", lo16(";
    printOp(MI->getOperand(2), true /* LoadAddrOp */);
    O << "-\"L0000" << LabelNumber << "$pb\")";
    O << "(";
    if (MI->getOperand(1).getReg() == PPC::R0)
      O << "0";
    else
      printOp(MI->getOperand(1));
    O << ")\n";
  } else if (ArgCount == 3 && ArgType[1] == PPCII::Disimm16) {
    printOp(MI->getOperand(0));
    O << ", ";
    printImmOp(MI->getOperand(1), ArgType[1]);
    O << "(";
    if (MI->getOperand(2).hasAllocatedReg() &&
        MI->getOperand(2).getReg() == PPC::R0)
      O << "0";
    else
      printOp(MI->getOperand(2));
    O << ")\n";
  } else {
    for (i = 0; i < ArgCount; ++i) {
      // addi and friends
      if (i == 1 && ArgCount == 3 && ArgType[2] == PPCII::Simm16 &&
          MI->getOperand(1).hasAllocatedReg() && 
          MI->getOperand(1).getReg() == PPC::R0) {
        O << "0";
      // for long branch support, bc $+8
      } else if (i == 1 && ArgCount == 2 && MI->getOperand(1).isImmediate() &&
                 TII.isBranch(MI->getOpcode())) {
        O << "$+8";
        assert(8 == MI->getOperand(i).getImmedValue()
          && "branch off PC not to pc+8?");
        //printOp(MI->getOperand(i));
      } else if (MI->getOperand(i).isImmediate()) {
        printImmOp(MI->getOperand(i), ArgType[i]);
      } else {
        printOp(MI->getOperand(i));
      }
      if (ArgCount - 1 == i)
        O << "\n";
      else
        O << ", ";
    }
  }
  return;
}

// SwitchSection - Switch to the specified section of the executable if we are
// not already in it!
//
static void SwitchSection(std::ostream &OS, std::string &CurSection,
                          const char *NewSection) {
  if (CurSection != NewSection) {
    CurSection = NewSection;
    if (!CurSection.empty())
      OS << "\t" << NewSection << "\n";
  }
}

bool PPC32AsmPrinter::doFinalization(Module &M) {
  const TargetData &TD = TM.getTargetData();
  std::string CurSection;

  // Print out module-level global variables here.
  for (Module::const_giterator I = M.gbegin(), E = M.gend(); I != E; ++I)
    if (I->hasInitializer()) {   // External global require no code
      O << "\n\n";
      std::string name = Mang->getValueName(I);
      Constant *C = I->getInitializer();
      unsigned Size = TD.getTypeSize(C->getType());
      unsigned Align = TD.getTypeAlignmentShift(C->getType());

      if (C->isNullValue() && /* FIXME: Verify correct */
          (I->hasInternalLinkage() || I->hasWeakLinkage())) {
        SwitchSection(O, CurSection, ".data");
        if (I->hasInternalLinkage())
          O << ".lcomm " << name << "," << TD.getTypeSize(C->getType())
            << "," << Align;
        else 
          O << ".comm " << name << "," << TD.getTypeSize(C->getType());
        O << "\t\t; ";
        WriteAsOperand(O, I, true, true, &M);
        O << "\n";
      } else {
        switch (I->getLinkage()) {
        case GlobalValue::LinkOnceLinkage:
          O << ".section __TEXT,__textcoal_nt,coalesced,no_toc\n"
            << ".weak_definition " << name << '\n'
            << ".private_extern " << name << '\n'
            << ".section __DATA,__datacoal_nt,coalesced,no_toc\n";
          LinkOnceStubs.insert(name);
          break;  
        case GlobalValue::WeakLinkage:   // FIXME: Verify correct for weak.
          // Nonnull linkonce -> weak
          O << "\t.weak " << name << "\n";
          SwitchSection(O, CurSection, "");
          O << "\t.section\t.llvm.linkonce.d." << name << ",\"aw\",@progbits\n";
          break;
        case GlobalValue::AppendingLinkage:
          // FIXME: appending linkage variables should go into a section of
          // their name or something.  For now, just emit them as external.
        case GlobalValue::ExternalLinkage:
          // If external or appending, declare as a global symbol
          O << "\t.globl " << name << "\n";
          // FALL THROUGH
        case GlobalValue::InternalLinkage:
          SwitchSection(O, CurSection, ".data");
          break;
        }

        emitAlignment(Align);
        O << name << ":\t\t\t\t; ";
        WriteAsOperand(O, I, true, true, &M);
        O << " = ";
        WriteAsOperand(O, C, false, false, &M);
        O << "\n";
        emitGlobalConstant(C);
      }
    }

  // Output stubs for dynamically-linked functions
  for (std::set<std::string>::iterator i = FnStubs.begin(), e = FnStubs.end(); 
       i != e; ++i)
  {
    O << ".data\n";
    O << ".section __TEXT,__picsymbolstub1,symbol_stubs,pure_instructions,32\n";
    emitAlignment(2);
    O << "L" << *i << "$stub:\n";
    O << "\t.indirect_symbol " << *i << "\n";
    O << "\tmflr r0\n";
    O << "\tbcl 20,31,L0$" << *i << "\n";
    O << "L0$" << *i << ":\n";
    O << "\tmflr r11\n";
    O << "\taddis r11,r11,ha16(L" << *i << "$lazy_ptr-L0$" << *i << ")\n";
    O << "\tmtlr r0\n";
    O << "\tlwzu r12,lo16(L" << *i << "$lazy_ptr-L0$" << *i << ")(r11)\n";
    O << "\tmtctr r12\n";
    O << "\tbctr\n";
    O << ".data\n";
    O << ".lazy_symbol_pointer\n";
    O << "L" << *i << "$lazy_ptr:\n";
    O << "\t.indirect_symbol " << *i << "\n";
    O << "\t.long dyld_stub_binding_helper\n";
  }

  O << "\n";

  // Output stubs for external global variables
  if (GVStubs.begin() != GVStubs.end())
    O << ".data\n.non_lazy_symbol_pointer\n";
  for (std::set<std::string>::iterator i = GVStubs.begin(), e = GVStubs.end(); 
       i != e; ++i) {
    O << "L" << *i << "$non_lazy_ptr:\n";
    O << "\t.indirect_symbol " << *i << "\n";
    O << "\t.long\t0\n";
  }
  
  // Output stubs for link-once variables
  if (LinkOnceStubs.begin() != LinkOnceStubs.end())
    O << ".data\n.align 2\n";
  for (std::set<std::string>::iterator i = LinkOnceStubs.begin(), 
         e = LinkOnceStubs.end(); i != e; ++i) {
    O << "L" << *i << "$non_lazy_ptr:\n"
      << "\t.long\t" << *i << '\n';
  }
  
  AsmPrinter::doFinalization(M);
  return false; // success
}
