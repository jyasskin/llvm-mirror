//===-- ARMExpandPseudoInsts.cpp - Expand pseudo instructions -----*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that expands pseudo instructions into target
// instructions to allow proper scheduling, if-conversion, and other late
// optimizations. This pass should be run after register allocation but before
// the post-regalloc scheduling pass.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "arm-pseudo"
#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMRegisterInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Target/TargetRegisterInfo.h"
using namespace llvm;

namespace {
  class ARMExpandPseudo : public MachineFunctionPass {
  public:
    static char ID;
    ARMExpandPseudo() : MachineFunctionPass(ID) {}

    const TargetInstrInfo *TII;
    const TargetRegisterInfo *TRI;

    virtual bool runOnMachineFunction(MachineFunction &Fn);

    virtual const char *getPassName() const {
      return "ARM pseudo instruction expansion pass";
    }

  private:
    void TransferImpOps(MachineInstr &OldMI,
                        MachineInstrBuilder &UseMI, MachineInstrBuilder &DefMI);
    bool ExpandMBB(MachineBasicBlock &MBB);
    void ExpandVLD(MachineBasicBlock::iterator &MBBI);
    void ExpandVST(MachineBasicBlock::iterator &MBBI);
    void ExpandLaneOp(MachineBasicBlock::iterator &MBBI);
    void ExpandVTBL(MachineBasicBlock::iterator &MBBI,
                    unsigned Opc, bool IsExt, unsigned NumRegs);
  };
  char ARMExpandPseudo::ID = 0;
}

/// TransferImpOps - Transfer implicit operands on the pseudo instruction to
/// the instructions created from the expansion.
void ARMExpandPseudo::TransferImpOps(MachineInstr &OldMI,
                                     MachineInstrBuilder &UseMI,
                                     MachineInstrBuilder &DefMI) {
  const TargetInstrDesc &Desc = OldMI.getDesc();
  for (unsigned i = Desc.getNumOperands(), e = OldMI.getNumOperands();
       i != e; ++i) {
    const MachineOperand &MO = OldMI.getOperand(i);
    assert(MO.isReg() && MO.getReg());
    if (MO.isUse())
      UseMI.addOperand(MO);
    else
      DefMI.addOperand(MO);
  }
}

namespace {
  // Constants for register spacing in NEON load/store instructions.
  // For quad-register load-lane and store-lane pseudo instructors, the
  // spacing is initially assumed to be EvenDblSpc, and that is changed to
  // OddDblSpc depending on the lane number operand.
  enum NEONRegSpacing {
    SingleSpc,
    EvenDblSpc,
    OddDblSpc
  };

  // Entries for NEON load/store information table.  The table is sorted by
  // PseudoOpc for fast binary-search lookups.
  struct NEONLdStTableEntry {
    unsigned PseudoOpc;
    unsigned RealOpc;
    bool IsLoad;
    bool HasWriteBack;
    NEONRegSpacing RegSpacing;
    unsigned char NumRegs; // D registers loaded or stored
    unsigned char RegElts; // elements per D register; used for lane ops

    // Comparison methods for binary search of the table.
    bool operator<(const NEONLdStTableEntry &TE) const {
      return PseudoOpc < TE.PseudoOpc;
    }
    friend bool operator<(const NEONLdStTableEntry &TE, unsigned PseudoOpc) {
      return TE.PseudoOpc < PseudoOpc;
    }
    friend bool ATTRIBUTE_UNUSED operator<(unsigned PseudoOpc,
                                           const NEONLdStTableEntry &TE) {
      return PseudoOpc < TE.PseudoOpc;
    }
  };
}

static const NEONLdStTableEntry NEONLdStTable[] = {
{ ARM::VLD1d64QPseudo,      ARM::VLD1d64Q,     true,  false, SingleSpc,  4, 1 },
{ ARM::VLD1d64QPseudo_UPD,  ARM::VLD1d64Q_UPD, true,  true,  SingleSpc,  4, 1 },
{ ARM::VLD1d64TPseudo,      ARM::VLD1d64T,     true,  false, SingleSpc,  3, 1 },
{ ARM::VLD1d64TPseudo_UPD,  ARM::VLD1d64T_UPD, true,  true,  SingleSpc,  3, 1 },

{ ARM::VLD1q16Pseudo,       ARM::VLD1q16,      true,  false, SingleSpc,  2, 4 },
{ ARM::VLD1q16Pseudo_UPD,   ARM::VLD1q16_UPD,  true,  true,  SingleSpc,  2, 4 },
{ ARM::VLD1q32Pseudo,       ARM::VLD1q32,      true,  false, SingleSpc,  2, 2 },
{ ARM::VLD1q32Pseudo_UPD,   ARM::VLD1q32_UPD,  true,  true,  SingleSpc,  2, 2 },
{ ARM::VLD1q64Pseudo,       ARM::VLD1q64,      true,  false, SingleSpc,  2, 1 },
{ ARM::VLD1q64Pseudo_UPD,   ARM::VLD1q64_UPD,  true,  true,  SingleSpc,  2, 1 },
{ ARM::VLD1q8Pseudo,        ARM::VLD1q8,       true,  false, SingleSpc,  2, 8 },
{ ARM::VLD1q8Pseudo_UPD,    ARM::VLD1q8_UPD,   true,  true,  SingleSpc,  2, 8 },

{ ARM::VLD2LNd16Pseudo,     ARM::VLD2LNd16,     true, false, SingleSpc,  2, 4 },
{ ARM::VLD2LNd16Pseudo_UPD, ARM::VLD2LNd16_UPD, true, true,  SingleSpc,  2, 4 },
{ ARM::VLD2LNd32Pseudo,     ARM::VLD2LNd32,     true, false, SingleSpc,  2, 2 },
{ ARM::VLD2LNd32Pseudo_UPD, ARM::VLD2LNd32_UPD, true, true,  SingleSpc,  2, 2 },
{ ARM::VLD2LNd8Pseudo,      ARM::VLD2LNd8,      true, false, SingleSpc,  2, 8 },
{ ARM::VLD2LNd8Pseudo_UPD,  ARM::VLD2LNd8_UPD,  true, true,  SingleSpc,  2, 8 },
{ ARM::VLD2LNq16Pseudo,     ARM::VLD2LNq16,     true, false, EvenDblSpc, 2, 4 },
{ ARM::VLD2LNq16Pseudo_UPD, ARM::VLD2LNq16_UPD, true, true,  EvenDblSpc, 2, 4 },
{ ARM::VLD2LNq32Pseudo,     ARM::VLD2LNq32,     true, false, EvenDblSpc, 2, 2 },
{ ARM::VLD2LNq32Pseudo_UPD, ARM::VLD2LNq32_UPD, true, true,  EvenDblSpc, 2, 2 },

{ ARM::VLD2d16Pseudo,       ARM::VLD2d16,      true,  false, SingleSpc,  2, 4 },
{ ARM::VLD2d16Pseudo_UPD,   ARM::VLD2d16_UPD,  true,  true,  SingleSpc,  2, 4 },
{ ARM::VLD2d32Pseudo,       ARM::VLD2d32,      true,  false, SingleSpc,  2, 2 },
{ ARM::VLD2d32Pseudo_UPD,   ARM::VLD2d32_UPD,  true,  true,  SingleSpc,  2, 2 },
{ ARM::VLD2d8Pseudo,        ARM::VLD2d8,       true,  false, SingleSpc,  2, 8 },
{ ARM::VLD2d8Pseudo_UPD,    ARM::VLD2d8_UPD,   true,  true,  SingleSpc,  2, 8 },

{ ARM::VLD2q16Pseudo,       ARM::VLD2q16,      true,  false, SingleSpc,  4, 4 },
{ ARM::VLD2q16Pseudo_UPD,   ARM::VLD2q16_UPD,  true,  true,  SingleSpc,  4, 4 },
{ ARM::VLD2q32Pseudo,       ARM::VLD2q32,      true,  false, SingleSpc,  4, 2 },
{ ARM::VLD2q32Pseudo_UPD,   ARM::VLD2q32_UPD,  true,  true,  SingleSpc,  4, 2 },
{ ARM::VLD2q8Pseudo,        ARM::VLD2q8,       true,  false, SingleSpc,  4, 8 },
{ ARM::VLD2q8Pseudo_UPD,    ARM::VLD2q8_UPD,   true,  true,  SingleSpc,  4, 8 },

{ ARM::VLD3LNd16Pseudo,     ARM::VLD3LNd16,     true, false, SingleSpc,  3, 4 },
{ ARM::VLD3LNd16Pseudo_UPD, ARM::VLD3LNd16_UPD, true, true,  SingleSpc,  3, 4 },
{ ARM::VLD3LNd32Pseudo,     ARM::VLD3LNd32,     true, false, SingleSpc,  3, 2 },
{ ARM::VLD3LNd32Pseudo_UPD, ARM::VLD3LNd32_UPD, true, true,  SingleSpc,  3, 2 },
{ ARM::VLD3LNd8Pseudo,      ARM::VLD3LNd8,      true, false, SingleSpc,  3, 8 },
{ ARM::VLD3LNd8Pseudo_UPD,  ARM::VLD3LNd8_UPD,  true, true,  SingleSpc,  3, 8 },
{ ARM::VLD3LNq16Pseudo,     ARM::VLD3LNq16,     true, false, EvenDblSpc, 3, 4 },
{ ARM::VLD3LNq16Pseudo_UPD, ARM::VLD3LNq16_UPD, true, true,  EvenDblSpc, 3, 4 },
{ ARM::VLD3LNq32Pseudo,     ARM::VLD3LNq32,     true, false, EvenDblSpc, 3, 2 },
{ ARM::VLD3LNq32Pseudo_UPD, ARM::VLD3LNq32_UPD, true, true,  EvenDblSpc, 3, 2 },

{ ARM::VLD3d16Pseudo,       ARM::VLD3d16,      true,  false, SingleSpc,  3, 4 },
{ ARM::VLD3d16Pseudo_UPD,   ARM::VLD3d16_UPD,  true,  true,  SingleSpc,  3, 4 },
{ ARM::VLD3d32Pseudo,       ARM::VLD3d32,      true,  false, SingleSpc,  3, 2 },
{ ARM::VLD3d32Pseudo_UPD,   ARM::VLD3d32_UPD,  true,  true,  SingleSpc,  3, 2 },
{ ARM::VLD3d8Pseudo,        ARM::VLD3d8,       true,  false, SingleSpc,  3, 8 },
{ ARM::VLD3d8Pseudo_UPD,    ARM::VLD3d8_UPD,   true,  true,  SingleSpc,  3, 8 },

{ ARM::VLD3q16Pseudo_UPD,    ARM::VLD3q16_UPD, true,  true,  EvenDblSpc, 3, 4 },
{ ARM::VLD3q16oddPseudo_UPD, ARM::VLD3q16_UPD, true,  true,  OddDblSpc,  3, 4 },
{ ARM::VLD3q32Pseudo_UPD,    ARM::VLD3q32_UPD, true,  true,  EvenDblSpc, 3, 2 },
{ ARM::VLD3q32oddPseudo_UPD, ARM::VLD3q32_UPD, true,  true,  OddDblSpc,  3, 2 },
{ ARM::VLD3q8Pseudo_UPD,     ARM::VLD3q8_UPD,  true,  true,  EvenDblSpc, 3, 8 },
{ ARM::VLD3q8oddPseudo_UPD,  ARM::VLD3q8_UPD,  true,  true,  OddDblSpc,  3, 8 },

{ ARM::VLD4LNd16Pseudo,     ARM::VLD4LNd16,     true, false, SingleSpc,  4, 4 },
{ ARM::VLD4LNd16Pseudo_UPD, ARM::VLD4LNd16_UPD, true, true,  SingleSpc,  4, 4 },
{ ARM::VLD4LNd32Pseudo,     ARM::VLD4LNd32,     true, false, SingleSpc,  4, 2 },
{ ARM::VLD4LNd32Pseudo_UPD, ARM::VLD4LNd32_UPD, true, true,  SingleSpc,  4, 2 },
{ ARM::VLD4LNd8Pseudo,      ARM::VLD4LNd8,      true, false, SingleSpc,  4, 8 },
{ ARM::VLD4LNd8Pseudo_UPD,  ARM::VLD4LNd8_UPD,  true, true,  SingleSpc,  4, 8 },
{ ARM::VLD4LNq16Pseudo,     ARM::VLD4LNq16,     true, false, EvenDblSpc, 4, 4 },
{ ARM::VLD4LNq16Pseudo_UPD, ARM::VLD4LNq16_UPD, true, true,  EvenDblSpc, 4, 4 },
{ ARM::VLD4LNq32Pseudo,     ARM::VLD4LNq32,     true, false, EvenDblSpc, 4, 2 },
{ ARM::VLD4LNq32Pseudo_UPD, ARM::VLD4LNq32_UPD, true, true,  EvenDblSpc, 4, 2 },

{ ARM::VLD4d16Pseudo,       ARM::VLD4d16,      true,  false, SingleSpc,  4, 4 },
{ ARM::VLD4d16Pseudo_UPD,   ARM::VLD4d16_UPD,  true,  true,  SingleSpc,  4, 4 },
{ ARM::VLD4d32Pseudo,       ARM::VLD4d32,      true,  false, SingleSpc,  4, 2 },
{ ARM::VLD4d32Pseudo_UPD,   ARM::VLD4d32_UPD,  true,  true,  SingleSpc,  4, 2 },
{ ARM::VLD4d8Pseudo,        ARM::VLD4d8,       true,  false, SingleSpc,  4, 8 },
{ ARM::VLD4d8Pseudo_UPD,    ARM::VLD4d8_UPD,   true,  true,  SingleSpc,  4, 8 },

{ ARM::VLD4q16Pseudo_UPD,    ARM::VLD4q16_UPD, true,  true,  EvenDblSpc, 4, 4 },
{ ARM::VLD4q16oddPseudo_UPD, ARM::VLD4q16_UPD, true,  true,  OddDblSpc,  4, 4 },
{ ARM::VLD4q32Pseudo_UPD,    ARM::VLD4q32_UPD, true,  true,  EvenDblSpc, 4, 2 },
{ ARM::VLD4q32oddPseudo_UPD, ARM::VLD4q32_UPD, true,  true,  OddDblSpc,  4, 2 },
{ ARM::VLD4q8Pseudo_UPD,     ARM::VLD4q8_UPD,  true,  true,  EvenDblSpc, 4, 8 },
{ ARM::VLD4q8oddPseudo_UPD,  ARM::VLD4q8_UPD,  true,  true,  OddDblSpc,  4, 8 },

{ ARM::VST1d64QPseudo,      ARM::VST1d64Q,     false, false, SingleSpc,  4, 1 },
{ ARM::VST1d64QPseudo_UPD,  ARM::VST1d64Q_UPD, false, true,  SingleSpc,  4, 1 },
{ ARM::VST1d64TPseudo,      ARM::VST1d64T,     false, false, SingleSpc,  3, 1 },
{ ARM::VST1d64TPseudo_UPD,  ARM::VST1d64T_UPD, false, true,  SingleSpc,  3, 1 },

{ ARM::VST1q16Pseudo,       ARM::VST1q16,      false, false, SingleSpc,  2, 4 },
{ ARM::VST1q16Pseudo_UPD,   ARM::VST1q16_UPD,  false, true,  SingleSpc,  2, 4 },
{ ARM::VST1q32Pseudo,       ARM::VST1q32,      false, false, SingleSpc,  2, 2 },
{ ARM::VST1q32Pseudo_UPD,   ARM::VST1q32_UPD,  false, true,  SingleSpc,  2, 2 },
{ ARM::VST1q64Pseudo,       ARM::VST1q64,      false, false, SingleSpc,  2, 1 },
{ ARM::VST1q64Pseudo_UPD,   ARM::VST1q64_UPD,  false, true,  SingleSpc,  2, 1 },
{ ARM::VST1q8Pseudo,        ARM::VST1q8,       false, false, SingleSpc,  2, 8 },
{ ARM::VST1q8Pseudo_UPD,    ARM::VST1q8_UPD,   false, true,  SingleSpc,  2, 8 },

{ ARM::VST2LNd16Pseudo,     ARM::VST2LNd16,     false, false, SingleSpc, 2, 4 },
{ ARM::VST2LNd16Pseudo_UPD, ARM::VST2LNd16_UPD, false, true,  SingleSpc, 2, 4 },
{ ARM::VST2LNd32Pseudo,     ARM::VST2LNd32,     false, false, SingleSpc, 2, 2 },
{ ARM::VST2LNd32Pseudo_UPD, ARM::VST2LNd32_UPD, false, true,  SingleSpc, 2, 2 },
{ ARM::VST2LNd8Pseudo,      ARM::VST2LNd8,      false, false, SingleSpc, 2, 8 },
{ ARM::VST2LNd8Pseudo_UPD,  ARM::VST2LNd8_UPD,  false, true,  SingleSpc, 2, 8 },
{ ARM::VST2LNq16Pseudo,     ARM::VST2LNq16,     false, false, EvenDblSpc, 2, 4},
{ ARM::VST2LNq16Pseudo_UPD, ARM::VST2LNq16_UPD, false, true,  EvenDblSpc, 2, 4},
{ ARM::VST2LNq32Pseudo,     ARM::VST2LNq32,     false, false, EvenDblSpc, 2, 2},
{ ARM::VST2LNq32Pseudo_UPD, ARM::VST2LNq32_UPD, false, true,  EvenDblSpc, 2, 2},

{ ARM::VST2d16Pseudo,       ARM::VST2d16,      false, false, SingleSpc,  2, 4 },
{ ARM::VST2d16Pseudo_UPD,   ARM::VST2d16_UPD,  false, true,  SingleSpc,  2, 4 },
{ ARM::VST2d32Pseudo,       ARM::VST2d32,      false, false, SingleSpc,  2, 2 },
{ ARM::VST2d32Pseudo_UPD,   ARM::VST2d32_UPD,  false, true,  SingleSpc,  2, 2 },
{ ARM::VST2d8Pseudo,        ARM::VST2d8,       false, false, SingleSpc,  2, 8 },
{ ARM::VST2d8Pseudo_UPD,    ARM::VST2d8_UPD,   false, true,  SingleSpc,  2, 8 },

{ ARM::VST2q16Pseudo,       ARM::VST2q16,      false, false, SingleSpc,  4, 4 },
{ ARM::VST2q16Pseudo_UPD,   ARM::VST2q16_UPD,  false, true,  SingleSpc,  4, 4 },
{ ARM::VST2q32Pseudo,       ARM::VST2q32,      false, false, SingleSpc,  4, 2 },
{ ARM::VST2q32Pseudo_UPD,   ARM::VST2q32_UPD,  false, true,  SingleSpc,  4, 2 },
{ ARM::VST2q8Pseudo,        ARM::VST2q8,       false, false, SingleSpc,  4, 8 },
{ ARM::VST2q8Pseudo_UPD,    ARM::VST2q8_UPD,   false, true,  SingleSpc,  4, 8 },

{ ARM::VST3LNd16Pseudo,     ARM::VST3LNd16,     false, false, SingleSpc, 3, 4 },
{ ARM::VST3LNd16Pseudo_UPD, ARM::VST3LNd16_UPD, false, true,  SingleSpc, 3, 4 },
{ ARM::VST3LNd32Pseudo,     ARM::VST3LNd32,     false, false, SingleSpc, 3, 2 },
{ ARM::VST3LNd32Pseudo_UPD, ARM::VST3LNd32_UPD, false, true,  SingleSpc, 3, 2 },
{ ARM::VST3LNd8Pseudo,      ARM::VST3LNd8,      false, false, SingleSpc, 3, 8 },
{ ARM::VST3LNd8Pseudo_UPD,  ARM::VST3LNd8_UPD,  false, true,  SingleSpc, 3, 8 },
{ ARM::VST3LNq16Pseudo,     ARM::VST3LNq16,     false, false, EvenDblSpc, 3, 4},
{ ARM::VST3LNq16Pseudo_UPD, ARM::VST3LNq16_UPD, false, true,  EvenDblSpc, 3, 4},
{ ARM::VST3LNq32Pseudo,     ARM::VST3LNq32,     false, false, EvenDblSpc, 3, 2},
{ ARM::VST3LNq32Pseudo_UPD, ARM::VST3LNq32_UPD, false, true,  EvenDblSpc, 3, 2},

{ ARM::VST3d16Pseudo,       ARM::VST3d16,      false, false, SingleSpc,  3, 4 },
{ ARM::VST3d16Pseudo_UPD,   ARM::VST3d16_UPD,  false, true,  SingleSpc,  3, 4 },
{ ARM::VST3d32Pseudo,       ARM::VST3d32,      false, false, SingleSpc,  3, 2 },
{ ARM::VST3d32Pseudo_UPD,   ARM::VST3d32_UPD,  false, true,  SingleSpc,  3, 2 },
{ ARM::VST3d8Pseudo,        ARM::VST3d8,       false, false, SingleSpc,  3, 8 },
{ ARM::VST3d8Pseudo_UPD,    ARM::VST3d8_UPD,   false, true,  SingleSpc,  3, 8 },

{ ARM::VST3q16Pseudo_UPD,    ARM::VST3q16_UPD, false, true,  EvenDblSpc, 3, 4 },
{ ARM::VST3q16oddPseudo_UPD, ARM::VST3q16_UPD, false, true,  OddDblSpc,  3, 4 },
{ ARM::VST3q32Pseudo_UPD,    ARM::VST3q32_UPD, false, true,  EvenDblSpc, 3, 2 },
{ ARM::VST3q32oddPseudo_UPD, ARM::VST3q32_UPD, false, true,  OddDblSpc,  3, 2 },
{ ARM::VST3q8Pseudo_UPD,     ARM::VST3q8_UPD,  false, true,  EvenDblSpc, 3, 8 },
{ ARM::VST3q8oddPseudo_UPD,  ARM::VST3q8_UPD,  false, true,  OddDblSpc,  3, 8 },

{ ARM::VST4LNd16Pseudo,     ARM::VST4LNd16,     false, false, SingleSpc, 4, 4 },
{ ARM::VST4LNd16Pseudo_UPD, ARM::VST4LNd16_UPD, false, true,  SingleSpc, 4, 4 },
{ ARM::VST4LNd32Pseudo,     ARM::VST4LNd32,     false, false, SingleSpc, 4, 2 },
{ ARM::VST4LNd32Pseudo_UPD, ARM::VST4LNd32_UPD, false, true,  SingleSpc, 4, 2 },
{ ARM::VST4LNd8Pseudo,      ARM::VST4LNd8,      false, false, SingleSpc, 4, 8 },
{ ARM::VST4LNd8Pseudo_UPD,  ARM::VST4LNd8_UPD,  false, true,  SingleSpc, 4, 8 },
{ ARM::VST4LNq16Pseudo,     ARM::VST4LNq16,     false, false, EvenDblSpc, 4, 4},
{ ARM::VST4LNq16Pseudo_UPD, ARM::VST4LNq16_UPD, false, true,  EvenDblSpc, 4, 4},
{ ARM::VST4LNq32Pseudo,     ARM::VST4LNq32,     false, false, EvenDblSpc, 4, 2},
{ ARM::VST4LNq32Pseudo_UPD, ARM::VST4LNq32_UPD, false, true,  EvenDblSpc, 4, 2},

{ ARM::VST4d16Pseudo,       ARM::VST4d16,      false, false, SingleSpc,  4, 4 },
{ ARM::VST4d16Pseudo_UPD,   ARM::VST4d16_UPD,  false, true,  SingleSpc,  4, 4 },
{ ARM::VST4d32Pseudo,       ARM::VST4d32,      false, false, SingleSpc,  4, 2 },
{ ARM::VST4d32Pseudo_UPD,   ARM::VST4d32_UPD,  false, true,  SingleSpc,  4, 2 },
{ ARM::VST4d8Pseudo,        ARM::VST4d8,       false, false, SingleSpc,  4, 8 },
{ ARM::VST4d8Pseudo_UPD,    ARM::VST4d8_UPD,   false, true,  SingleSpc,  4, 8 },

{ ARM::VST4q16Pseudo_UPD,    ARM::VST4q16_UPD, false, true,  EvenDblSpc, 4, 4 },
{ ARM::VST4q16oddPseudo_UPD, ARM::VST4q16_UPD, false, true,  OddDblSpc,  4, 4 },
{ ARM::VST4q32Pseudo_UPD,    ARM::VST4q32_UPD, false, true,  EvenDblSpc, 4, 2 },
{ ARM::VST4q32oddPseudo_UPD, ARM::VST4q32_UPD, false, true,  OddDblSpc,  4, 2 },
{ ARM::VST4q8Pseudo_UPD,     ARM::VST4q8_UPD,  false, true,  EvenDblSpc, 4, 8 },
{ ARM::VST4q8oddPseudo_UPD , ARM::VST4q8_UPD,  false, true,  OddDblSpc,  4, 8 }
};

/// LookupNEONLdSt - Search the NEONLdStTable for information about a NEON
/// load or store pseudo instruction.
static const NEONLdStTableEntry *LookupNEONLdSt(unsigned Opcode) {
  unsigned NumEntries = array_lengthof(NEONLdStTable);

#ifndef NDEBUG
  // Make sure the table is sorted.
  static bool TableChecked = false;
  if (!TableChecked) {
    for (unsigned i = 0; i != NumEntries-1; ++i)
      assert(NEONLdStTable[i] < NEONLdStTable[i+1] &&
             "NEONLdStTable is not sorted!");
    TableChecked = true;
  }
#endif

  const NEONLdStTableEntry *I =
    std::lower_bound(NEONLdStTable, NEONLdStTable + NumEntries, Opcode);
  if (I != NEONLdStTable + NumEntries && I->PseudoOpc == Opcode)
    return I;
  return NULL;
}

/// GetDSubRegs - Get 4 D subregisters of a Q, QQ, or QQQQ register,
/// corresponding to the specified register spacing.  Not all of the results
/// are necessarily valid, e.g., a Q register only has 2 D subregisters.
static void GetDSubRegs(unsigned Reg, NEONRegSpacing RegSpc,
                        const TargetRegisterInfo *TRI, unsigned &D0,
                        unsigned &D1, unsigned &D2, unsigned &D3) {
  if (RegSpc == SingleSpc) {
    D0 = TRI->getSubReg(Reg, ARM::dsub_0);
    D1 = TRI->getSubReg(Reg, ARM::dsub_1);
    D2 = TRI->getSubReg(Reg, ARM::dsub_2);
    D3 = TRI->getSubReg(Reg, ARM::dsub_3);
  } else if (RegSpc == EvenDblSpc) {
    D0 = TRI->getSubReg(Reg, ARM::dsub_0);
    D1 = TRI->getSubReg(Reg, ARM::dsub_2);
    D2 = TRI->getSubReg(Reg, ARM::dsub_4);
    D3 = TRI->getSubReg(Reg, ARM::dsub_6);
  } else {
    assert(RegSpc == OddDblSpc && "unknown register spacing");
    D0 = TRI->getSubReg(Reg, ARM::dsub_1);
    D1 = TRI->getSubReg(Reg, ARM::dsub_3);
    D2 = TRI->getSubReg(Reg, ARM::dsub_5);
    D3 = TRI->getSubReg(Reg, ARM::dsub_7);
  }
}

/// ExpandVLD - Translate VLD pseudo instructions with Q, QQ or QQQQ register
/// operands to real VLD instructions with D register operands.
void ARMExpandPseudo::ExpandVLD(MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock &MBB = *MI.getParent();

  const NEONLdStTableEntry *TableEntry = LookupNEONLdSt(MI.getOpcode());
  assert(TableEntry && TableEntry->IsLoad && "NEONLdStTable lookup failed");
  NEONRegSpacing RegSpc = TableEntry->RegSpacing;
  unsigned NumRegs = TableEntry->NumRegs;

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                    TII->get(TableEntry->RealOpc));
  unsigned OpIdx = 0;

  bool DstIsDead = MI.getOperand(OpIdx).isDead();
  unsigned DstReg = MI.getOperand(OpIdx++).getReg();
  unsigned D0, D1, D2, D3;
  GetDSubRegs(DstReg, RegSpc, TRI, D0, D1, D2, D3);
  MIB.addReg(D0, RegState::Define | getDeadRegState(DstIsDead))
    .addReg(D1, RegState::Define | getDeadRegState(DstIsDead));
  if (NumRegs > 2)
    MIB.addReg(D2, RegState::Define | getDeadRegState(DstIsDead));
  if (NumRegs > 3)
    MIB.addReg(D3, RegState::Define | getDeadRegState(DstIsDead));

  if (TableEntry->HasWriteBack)
    MIB.addOperand(MI.getOperand(OpIdx++));

  // Copy the addrmode6 operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));
  // Copy the am6offset operand.
  if (TableEntry->HasWriteBack)
    MIB.addOperand(MI.getOperand(OpIdx++));

  // For an instruction writing double-spaced subregs, the pseudo instruction
  // has an extra operand that is a use of the super-register.  Record the
  // operand index and skip over it.
  unsigned SrcOpIdx = 0;
  if (RegSpc == EvenDblSpc || RegSpc == OddDblSpc)
    SrcOpIdx = OpIdx++;

  // Copy the predicate operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));

  // Copy the super-register source operand used for double-spaced subregs over
  // to the new instruction as an implicit operand.
  if (SrcOpIdx != 0) {
    MachineOperand MO = MI.getOperand(SrcOpIdx);
    MO.setImplicit(true);
    MIB.addOperand(MO);
  }
  // Add an implicit def for the super-register.
  MIB.addReg(DstReg, RegState::ImplicitDefine | getDeadRegState(DstIsDead));
  TransferImpOps(MI, MIB, MIB);
  MI.eraseFromParent();
}

/// ExpandVST - Translate VST pseudo instructions with Q, QQ or QQQQ register
/// operands to real VST instructions with D register operands.
void ARMExpandPseudo::ExpandVST(MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock &MBB = *MI.getParent();

  const NEONLdStTableEntry *TableEntry = LookupNEONLdSt(MI.getOpcode());
  assert(TableEntry && !TableEntry->IsLoad && "NEONLdStTable lookup failed");
  NEONRegSpacing RegSpc = TableEntry->RegSpacing;
  unsigned NumRegs = TableEntry->NumRegs;

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                    TII->get(TableEntry->RealOpc));
  unsigned OpIdx = 0;
  if (TableEntry->HasWriteBack)
    MIB.addOperand(MI.getOperand(OpIdx++));

  // Copy the addrmode6 operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));
  // Copy the am6offset operand.
  if (TableEntry->HasWriteBack)
    MIB.addOperand(MI.getOperand(OpIdx++));

  bool SrcIsKill = MI.getOperand(OpIdx).isKill();
  unsigned SrcReg = MI.getOperand(OpIdx++).getReg();
  unsigned D0, D1, D2, D3;
  GetDSubRegs(SrcReg, RegSpc, TRI, D0, D1, D2, D3);
  MIB.addReg(D0).addReg(D1);
  if (NumRegs > 2)
    MIB.addReg(D2);
  if (NumRegs > 3)
    MIB.addReg(D3);

  // Copy the predicate operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));

  if (SrcIsKill)
    // Add an implicit kill for the super-reg.
    (*MIB).addRegisterKilled(SrcReg, TRI, true);
  TransferImpOps(MI, MIB, MIB);
  MI.eraseFromParent();
}

/// ExpandLaneOp - Translate VLD*LN and VST*LN instructions with Q, QQ or QQQQ
/// register operands to real instructions with D register operands.
void ARMExpandPseudo::ExpandLaneOp(MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock &MBB = *MI.getParent();

  const NEONLdStTableEntry *TableEntry = LookupNEONLdSt(MI.getOpcode());
  assert(TableEntry && "NEONLdStTable lookup failed");
  NEONRegSpacing RegSpc = TableEntry->RegSpacing;
  unsigned NumRegs = TableEntry->NumRegs;
  unsigned RegElts = TableEntry->RegElts;

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                    TII->get(TableEntry->RealOpc));
  unsigned OpIdx = 0;
  // The lane operand is always the 3rd from last operand, before the 2
  // predicate operands.
  unsigned Lane = MI.getOperand(MI.getDesc().getNumOperands() - 3).getImm();

  // Adjust the lane and spacing as needed for Q registers.
  assert(RegSpc != OddDblSpc && "unexpected register spacing for VLD/VST-lane");
  if (RegSpc == EvenDblSpc && Lane >= RegElts) {
    RegSpc = OddDblSpc;
    Lane -= RegElts;
  }
  assert(Lane < RegElts && "out of range lane for VLD/VST-lane");

  unsigned D0, D1, D2, D3;
  unsigned DstReg = 0;
  bool DstIsDead = false;
  if (TableEntry->IsLoad) {
    DstIsDead = MI.getOperand(OpIdx).isDead();
    DstReg = MI.getOperand(OpIdx++).getReg();
    GetDSubRegs(DstReg, RegSpc, TRI, D0, D1, D2, D3);
    MIB.addReg(D0, RegState::Define | getDeadRegState(DstIsDead))
      .addReg(D1, RegState::Define | getDeadRegState(DstIsDead));
    if (NumRegs > 2)
      MIB.addReg(D2, RegState::Define | getDeadRegState(DstIsDead));
    if (NumRegs > 3)
      MIB.addReg(D3, RegState::Define | getDeadRegState(DstIsDead));
  }

  if (TableEntry->HasWriteBack)
    MIB.addOperand(MI.getOperand(OpIdx++));

  // Copy the addrmode6 operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));
  // Copy the am6offset operand.
  if (TableEntry->HasWriteBack)
    MIB.addOperand(MI.getOperand(OpIdx++));

  // Grab the super-register source.
  MachineOperand MO = MI.getOperand(OpIdx++);
  if (!TableEntry->IsLoad)
    GetDSubRegs(MO.getReg(), RegSpc, TRI, D0, D1, D2, D3);

  // Add the subregs as sources of the new instruction.
  unsigned SrcFlags = (getUndefRegState(MO.isUndef()) |
                       getKillRegState(MO.isKill()));
  MIB.addReg(D0, SrcFlags).addReg(D1, SrcFlags);
  if (NumRegs > 2)
    MIB.addReg(D2, SrcFlags);
  if (NumRegs > 3)
    MIB.addReg(D3, SrcFlags);

  // Add the lane number operand.
  MIB.addImm(Lane);
  OpIdx += 1;

  // Copy the predicate operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));

  // Copy the super-register source to be an implicit source.
  MO.setImplicit(true);
  MIB.addOperand(MO);
  if (TableEntry->IsLoad)
    // Add an implicit def for the super-register.
    MIB.addReg(DstReg, RegState::ImplicitDefine | getDeadRegState(DstIsDead));
  TransferImpOps(MI, MIB, MIB);
  MI.eraseFromParent();
}

/// ExpandVTBL - Translate VTBL and VTBX pseudo instructions with Q or QQ
/// register operands to real instructions with D register operands.
void ARMExpandPseudo::ExpandVTBL(MachineBasicBlock::iterator &MBBI,
                                 unsigned Opc, bool IsExt, unsigned NumRegs) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock &MBB = *MI.getParent();

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(Opc));
  unsigned OpIdx = 0;

  // Transfer the destination register operand.
  MIB.addOperand(MI.getOperand(OpIdx++));
  if (IsExt)
    MIB.addOperand(MI.getOperand(OpIdx++));

  bool SrcIsKill = MI.getOperand(OpIdx).isKill();
  unsigned SrcReg = MI.getOperand(OpIdx++).getReg();
  unsigned D0, D1, D2, D3;
  GetDSubRegs(SrcReg, SingleSpc, TRI, D0, D1, D2, D3);
  MIB.addReg(D0).addReg(D1);
  if (NumRegs > 2)
    MIB.addReg(D2);
  if (NumRegs > 3)
    MIB.addReg(D3);

  // Copy the other source register operand.
  MIB.addOperand(MI.getOperand(OpIdx++));

  // Copy the predicate operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));

  if (SrcIsKill)
    // Add an implicit kill for the super-reg.
    (*MIB).addRegisterKilled(SrcReg, TRI, true);
  TransferImpOps(MI, MIB, MIB);
  MI.eraseFromParent();
}

bool ARMExpandPseudo::ExpandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineInstr &MI = *MBBI;
    MachineBasicBlock::iterator NMBBI = llvm::next(MBBI);

    bool ModifiedOp = true;
    unsigned Opcode = MI.getOpcode();
    switch (Opcode) {
    default:
      ModifiedOp = false;
      break;

    case ARM::tLDRpci_pic:
    case ARM::t2LDRpci_pic: {
      unsigned NewLdOpc = (Opcode == ARM::tLDRpci_pic)
        ? ARM::tLDRpci : ARM::t2LDRpci;
      unsigned DstReg = MI.getOperand(0).getReg();
      bool DstIsDead = MI.getOperand(0).isDead();
      MachineInstrBuilder MIB1 =
        AddDefaultPred(BuildMI(MBB, MBBI, MI.getDebugLoc(),
                               TII->get(NewLdOpc), DstReg)
                       .addOperand(MI.getOperand(1)));
      (*MIB1).setMemRefs(MI.memoperands_begin(), MI.memoperands_end());
      MachineInstrBuilder MIB2 = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                         TII->get(ARM::tPICADD))
        .addReg(DstReg, getDefRegState(true) | getDeadRegState(DstIsDead))
        .addReg(DstReg)
        .addOperand(MI.getOperand(2));
      TransferImpOps(MI, MIB1, MIB2);
      MI.eraseFromParent();
      break;
    }

    case ARM::MOVi32imm:
    case ARM::t2MOVi32imm: {
      unsigned PredReg = 0;
      ARMCC::CondCodes Pred = llvm::getInstrPredicate(&MI, PredReg);
      unsigned DstReg = MI.getOperand(0).getReg();
      bool DstIsDead = MI.getOperand(0).isDead();
      const MachineOperand &MO = MI.getOperand(1);
      MachineInstrBuilder LO16, HI16;

      LO16 = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                     TII->get(Opcode == ARM::MOVi32imm ?
                              ARM::MOVi16 : ARM::t2MOVi16),
                     DstReg);
      HI16 = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                     TII->get(Opcode == ARM::MOVi32imm ?
                              ARM::MOVTi16 : ARM::t2MOVTi16))
        .addReg(DstReg, getDefRegState(true) | getDeadRegState(DstIsDead))
        .addReg(DstReg);

      if (MO.isImm()) {
        unsigned Imm = MO.getImm();
        unsigned Lo16 = Imm & 0xffff;
        unsigned Hi16 = (Imm >> 16) & 0xffff;
        LO16 = LO16.addImm(Lo16);
        HI16 = HI16.addImm(Hi16);
      } else {
        const GlobalValue *GV = MO.getGlobal();
        unsigned TF = MO.getTargetFlags();
        LO16 = LO16.addGlobalAddress(GV, MO.getOffset(), TF | ARMII::MO_LO16);
        HI16 = HI16.addGlobalAddress(GV, MO.getOffset(), TF | ARMII::MO_HI16);
      }
      (*LO16).setMemRefs(MI.memoperands_begin(), MI.memoperands_end());
      (*HI16).setMemRefs(MI.memoperands_begin(), MI.memoperands_end());
      LO16.addImm(Pred).addReg(PredReg);
      HI16.addImm(Pred).addReg(PredReg);
      TransferImpOps(MI, LO16, HI16);
      MI.eraseFromParent();
      break;
    }

    case ARM::VMOVQQ: {
      unsigned DstReg = MI.getOperand(0).getReg();
      bool DstIsDead = MI.getOperand(0).isDead();
      unsigned EvenDst = TRI->getSubReg(DstReg, ARM::qsub_0);
      unsigned OddDst  = TRI->getSubReg(DstReg, ARM::qsub_1);
      unsigned SrcReg = MI.getOperand(1).getReg();
      bool SrcIsKill = MI.getOperand(1).isKill();
      unsigned EvenSrc = TRI->getSubReg(SrcReg, ARM::qsub_0);
      unsigned OddSrc  = TRI->getSubReg(SrcReg, ARM::qsub_1);
      MachineInstrBuilder Even =
        AddDefaultPred(BuildMI(MBB, MBBI, MI.getDebugLoc(),
                               TII->get(ARM::VMOVQ))
                     .addReg(EvenDst,
                             getDefRegState(true) | getDeadRegState(DstIsDead))
                     .addReg(EvenSrc, getKillRegState(SrcIsKill)));
      MachineInstrBuilder Odd =
        AddDefaultPred(BuildMI(MBB, MBBI, MI.getDebugLoc(),
                               TII->get(ARM::VMOVQ))
                     .addReg(OddDst,
                             getDefRegState(true) | getDeadRegState(DstIsDead))
                     .addReg(OddSrc, getKillRegState(SrcIsKill)));
      TransferImpOps(MI, Even, Odd);
      MI.eraseFromParent();
      break;
    }

    case ARM::VLDMQ: {
      MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::VLDMD));
      unsigned OpIdx = 0;
      // Grab the Q register destination.
      bool DstIsDead = MI.getOperand(OpIdx).isDead();
      unsigned DstReg = MI.getOperand(OpIdx++).getReg();
      // Copy the addrmode4 operands.
      MIB.addOperand(MI.getOperand(OpIdx++));
      MIB.addOperand(MI.getOperand(OpIdx++));
      // Copy the predicate operands.
      MIB.addOperand(MI.getOperand(OpIdx++));
      MIB.addOperand(MI.getOperand(OpIdx++));
      // Add the destination operands (D subregs).
      unsigned D0 = TRI->getSubReg(DstReg, ARM::dsub_0);
      unsigned D1 = TRI->getSubReg(DstReg, ARM::dsub_1);
      MIB.addReg(D0, RegState::Define | getDeadRegState(DstIsDead))
        .addReg(D1, RegState::Define | getDeadRegState(DstIsDead));
      // Add an implicit def for the super-register.
      MIB.addReg(DstReg, RegState::ImplicitDefine | getDeadRegState(DstIsDead));
      TransferImpOps(MI, MIB, MIB);
      MI.eraseFromParent();
      break;
    }

    case ARM::VSTMQ: {
      MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::VSTMD));
      unsigned OpIdx = 0;
      // Grab the Q register source.
      bool SrcIsKill = MI.getOperand(OpIdx).isKill();
      unsigned SrcReg = MI.getOperand(OpIdx++).getReg();
      // Copy the addrmode4 operands.
      MIB.addOperand(MI.getOperand(OpIdx++));
      MIB.addOperand(MI.getOperand(OpIdx++));
      // Copy the predicate operands.
      MIB.addOperand(MI.getOperand(OpIdx++));
      MIB.addOperand(MI.getOperand(OpIdx++));
      // Add the source operands (D subregs).
      unsigned D0 = TRI->getSubReg(SrcReg, ARM::dsub_0);
      unsigned D1 = TRI->getSubReg(SrcReg, ARM::dsub_1);
      MIB.addReg(D0).addReg(D1);
      if (SrcIsKill)
        // Add an implicit kill for the Q register.
        (*MIB).addRegisterKilled(SrcReg, TRI, true);
      TransferImpOps(MI, MIB, MIB);
      MI.eraseFromParent();
      break;
    }
    case ARM::VDUPfqf:
    case ARM::VDUPfdf:{
      unsigned NewOpc = Opcode == ARM::VDUPfqf ? ARM::VDUPLNfq : ARM::VDUPLNfd;
      MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewOpc));
      unsigned OpIdx = 0;
      unsigned SrcReg = MI.getOperand(1).getReg();
      unsigned Lane = getARMRegisterNumbering(SrcReg) & 1;
      unsigned DReg = TRI->getMatchingSuperReg(SrcReg,
          Lane & 1 ? ARM::ssub_1 : ARM::ssub_0, &ARM::DPR_VFP2RegClass);
      // The lane is [0,1] for the containing DReg superregister.
      // Copy the dst/src register operands.
      MIB.addOperand(MI.getOperand(OpIdx++));
      MIB.addReg(DReg);
      ++OpIdx;
      // Add the lane select operand.
      MIB.addImm(Lane);
      // Add the predicate operands.
      MIB.addOperand(MI.getOperand(OpIdx++));
      MIB.addOperand(MI.getOperand(OpIdx++));

      TransferImpOps(MI, MIB, MIB);
      MI.eraseFromParent();
      break;
    }

    case ARM::VLD1q8Pseudo:
    case ARM::VLD1q16Pseudo:
    case ARM::VLD1q32Pseudo:
    case ARM::VLD1q64Pseudo:
    case ARM::VLD1q8Pseudo_UPD:
    case ARM::VLD1q16Pseudo_UPD:
    case ARM::VLD1q32Pseudo_UPD:
    case ARM::VLD1q64Pseudo_UPD:
    case ARM::VLD2d8Pseudo:
    case ARM::VLD2d16Pseudo:
    case ARM::VLD2d32Pseudo:
    case ARM::VLD2q8Pseudo:
    case ARM::VLD2q16Pseudo:
    case ARM::VLD2q32Pseudo:
    case ARM::VLD2d8Pseudo_UPD:
    case ARM::VLD2d16Pseudo_UPD:
    case ARM::VLD2d32Pseudo_UPD:
    case ARM::VLD2q8Pseudo_UPD:
    case ARM::VLD2q16Pseudo_UPD:
    case ARM::VLD2q32Pseudo_UPD:
    case ARM::VLD3d8Pseudo:
    case ARM::VLD3d16Pseudo:
    case ARM::VLD3d32Pseudo:
    case ARM::VLD1d64TPseudo:
    case ARM::VLD3d8Pseudo_UPD:
    case ARM::VLD3d16Pseudo_UPD:
    case ARM::VLD3d32Pseudo_UPD:
    case ARM::VLD1d64TPseudo_UPD:
    case ARM::VLD3q8Pseudo_UPD:
    case ARM::VLD3q16Pseudo_UPD:
    case ARM::VLD3q32Pseudo_UPD:
    case ARM::VLD3q8oddPseudo_UPD:
    case ARM::VLD3q16oddPseudo_UPD:
    case ARM::VLD3q32oddPseudo_UPD:
    case ARM::VLD4d8Pseudo:
    case ARM::VLD4d16Pseudo:
    case ARM::VLD4d32Pseudo:
    case ARM::VLD1d64QPseudo:
    case ARM::VLD4d8Pseudo_UPD:
    case ARM::VLD4d16Pseudo_UPD:
    case ARM::VLD4d32Pseudo_UPD:
    case ARM::VLD1d64QPseudo_UPD:
    case ARM::VLD4q8Pseudo_UPD:
    case ARM::VLD4q16Pseudo_UPD:
    case ARM::VLD4q32Pseudo_UPD:
    case ARM::VLD4q8oddPseudo_UPD:
    case ARM::VLD4q16oddPseudo_UPD:
    case ARM::VLD4q32oddPseudo_UPD:
      ExpandVLD(MBBI);
      break;

    case ARM::VST1q8Pseudo:
    case ARM::VST1q16Pseudo:
    case ARM::VST1q32Pseudo:
    case ARM::VST1q64Pseudo:
    case ARM::VST1q8Pseudo_UPD:
    case ARM::VST1q16Pseudo_UPD:
    case ARM::VST1q32Pseudo_UPD:
    case ARM::VST1q64Pseudo_UPD:
    case ARM::VST2d8Pseudo:
    case ARM::VST2d16Pseudo:
    case ARM::VST2d32Pseudo:
    case ARM::VST2q8Pseudo:
    case ARM::VST2q16Pseudo:
    case ARM::VST2q32Pseudo:
    case ARM::VST2d8Pseudo_UPD:
    case ARM::VST2d16Pseudo_UPD:
    case ARM::VST2d32Pseudo_UPD:
    case ARM::VST2q8Pseudo_UPD:
    case ARM::VST2q16Pseudo_UPD:
    case ARM::VST2q32Pseudo_UPD:
    case ARM::VST3d8Pseudo:
    case ARM::VST3d16Pseudo:
    case ARM::VST3d32Pseudo:
    case ARM::VST1d64TPseudo:
    case ARM::VST3d8Pseudo_UPD:
    case ARM::VST3d16Pseudo_UPD:
    case ARM::VST3d32Pseudo_UPD:
    case ARM::VST1d64TPseudo_UPD:
    case ARM::VST3q8Pseudo_UPD:
    case ARM::VST3q16Pseudo_UPD:
    case ARM::VST3q32Pseudo_UPD:
    case ARM::VST3q8oddPseudo_UPD:
    case ARM::VST3q16oddPseudo_UPD:
    case ARM::VST3q32oddPseudo_UPD:
    case ARM::VST4d8Pseudo:
    case ARM::VST4d16Pseudo:
    case ARM::VST4d32Pseudo:
    case ARM::VST1d64QPseudo:
    case ARM::VST4d8Pseudo_UPD:
    case ARM::VST4d16Pseudo_UPD:
    case ARM::VST4d32Pseudo_UPD:
    case ARM::VST1d64QPseudo_UPD:
    case ARM::VST4q8Pseudo_UPD:
    case ARM::VST4q16Pseudo_UPD:
    case ARM::VST4q32Pseudo_UPD:
    case ARM::VST4q8oddPseudo_UPD:
    case ARM::VST4q16oddPseudo_UPD:
    case ARM::VST4q32oddPseudo_UPD:
      ExpandVST(MBBI);
      break;

    case ARM::VLD2LNd8Pseudo:
    case ARM::VLD2LNd16Pseudo:
    case ARM::VLD2LNd32Pseudo:
    case ARM::VLD2LNq16Pseudo:
    case ARM::VLD2LNq32Pseudo:
    case ARM::VLD2LNd8Pseudo_UPD:
    case ARM::VLD2LNd16Pseudo_UPD:
    case ARM::VLD2LNd32Pseudo_UPD:
    case ARM::VLD2LNq16Pseudo_UPD:
    case ARM::VLD2LNq32Pseudo_UPD:
    case ARM::VLD3LNd8Pseudo:
    case ARM::VLD3LNd16Pseudo:
    case ARM::VLD3LNd32Pseudo:
    case ARM::VLD3LNq16Pseudo:
    case ARM::VLD3LNq32Pseudo:
    case ARM::VLD3LNd8Pseudo_UPD:
    case ARM::VLD3LNd16Pseudo_UPD:
    case ARM::VLD3LNd32Pseudo_UPD:
    case ARM::VLD3LNq16Pseudo_UPD:
    case ARM::VLD3LNq32Pseudo_UPD:
    case ARM::VLD4LNd8Pseudo:
    case ARM::VLD4LNd16Pseudo:
    case ARM::VLD4LNd32Pseudo:
    case ARM::VLD4LNq16Pseudo:
    case ARM::VLD4LNq32Pseudo:
    case ARM::VLD4LNd8Pseudo_UPD:
    case ARM::VLD4LNd16Pseudo_UPD:
    case ARM::VLD4LNd32Pseudo_UPD:
    case ARM::VLD4LNq16Pseudo_UPD:
    case ARM::VLD4LNq32Pseudo_UPD:
    case ARM::VST2LNd8Pseudo:
    case ARM::VST2LNd16Pseudo:
    case ARM::VST2LNd32Pseudo:
    case ARM::VST2LNq16Pseudo:
    case ARM::VST2LNq32Pseudo:
    case ARM::VST2LNd8Pseudo_UPD:
    case ARM::VST2LNd16Pseudo_UPD:
    case ARM::VST2LNd32Pseudo_UPD:
    case ARM::VST2LNq16Pseudo_UPD:
    case ARM::VST2LNq32Pseudo_UPD:
    case ARM::VST3LNd8Pseudo:
    case ARM::VST3LNd16Pseudo:
    case ARM::VST3LNd32Pseudo:
    case ARM::VST3LNq16Pseudo:
    case ARM::VST3LNq32Pseudo:
    case ARM::VST3LNd8Pseudo_UPD:
    case ARM::VST3LNd16Pseudo_UPD:
    case ARM::VST3LNd32Pseudo_UPD:
    case ARM::VST3LNq16Pseudo_UPD:
    case ARM::VST3LNq32Pseudo_UPD:
    case ARM::VST4LNd8Pseudo:
    case ARM::VST4LNd16Pseudo:
    case ARM::VST4LNd32Pseudo:
    case ARM::VST4LNq16Pseudo:
    case ARM::VST4LNq32Pseudo:
    case ARM::VST4LNd8Pseudo_UPD:
    case ARM::VST4LNd16Pseudo_UPD:
    case ARM::VST4LNd32Pseudo_UPD:
    case ARM::VST4LNq16Pseudo_UPD:
    case ARM::VST4LNq32Pseudo_UPD:
      ExpandLaneOp(MBBI);
      break;

    case ARM::VTBL2Pseudo:
      ExpandVTBL(MBBI, ARM::VTBL2, false, 2); break;
    case ARM::VTBL3Pseudo:
      ExpandVTBL(MBBI, ARM::VTBL3, false, 3); break;
    case ARM::VTBL4Pseudo:
      ExpandVTBL(MBBI, ARM::VTBL4, false, 4); break;
    case ARM::VTBX2Pseudo:
      ExpandVTBL(MBBI, ARM::VTBX2, true, 2); break;
    case ARM::VTBX3Pseudo:
      ExpandVTBL(MBBI, ARM::VTBX3, true, 3); break;
    case ARM::VTBX4Pseudo:
      ExpandVTBL(MBBI, ARM::VTBX4, true, 4); break;
    }

    if (ModifiedOp)
      Modified = true;
    MBBI = NMBBI;
  }

  return Modified;
}

bool ARMExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
  TII = MF.getTarget().getInstrInfo();
  TRI = MF.getTarget().getRegisterInfo();

  bool Modified = false;
  for (MachineFunction::iterator MFI = MF.begin(), E = MF.end(); MFI != E;
       ++MFI)
    Modified |= ExpandMBB(*MFI);
  return Modified;
}

/// createARMExpandPseudoPass - returns an instance of the pseudo instruction
/// expansion pass.
FunctionPass *llvm::createARMExpandPseudoPass() {
  return new ARMExpandPseudo();
}
