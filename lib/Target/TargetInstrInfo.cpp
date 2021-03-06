//===-- TargetInstrInfo.cpp - Target Instruction Information --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetInstrItineraries.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;

//===----------------------------------------------------------------------===//
//  TargetOperandInfo
//===----------------------------------------------------------------------===//

/// getRegClass - Get the register class for the operand, handling resolution
/// of "symbolic" pointer register classes etc.  If this is not a register
/// operand, this returns null.
const TargetRegisterClass *
TargetOperandInfo::getRegClass(const TargetRegisterInfo *TRI) const {
  if (isLookupPtrRegClass())
    return TRI->getPointerRegClass(RegClass);
  // Instructions like INSERT_SUBREG do not have fixed register classes.
  if (RegClass < 0)
    return 0;
  // Otherwise just look it up normally.
  return TRI->getRegClass(RegClass);
}

//===----------------------------------------------------------------------===//
//  TargetInstrInfo
//===----------------------------------------------------------------------===//

TargetInstrInfo::TargetInstrInfo(const TargetInstrDesc* Desc,
                                 unsigned numOpcodes)
  : Descriptors(Desc), NumOpcodes(numOpcodes) {
}

TargetInstrInfo::~TargetInstrInfo() {
}

unsigned
TargetInstrInfo::getNumMicroOps(const MachineInstr *MI,
                                const InstrItineraryData *ItinData) const {
  if (!ItinData || ItinData->isEmpty())
    return 1;

  unsigned Class = MI->getDesc().getSchedClass();
  unsigned UOps = ItinData->Itineraries[Class].NumMicroOps;
  if (UOps)
    return UOps;

  // The # of u-ops is dynamically determined. The specific target should
  // override this function to return the right number.
  return 1;
}

int
TargetInstrInfo::getOperandLatency(const InstrItineraryData *ItinData,
                             const MachineInstr *DefMI, unsigned DefIdx,
                             const MachineInstr *UseMI, unsigned UseIdx) const {
  if (!ItinData || ItinData->isEmpty())
    return -1;

  unsigned DefClass = DefMI->getDesc().getSchedClass();
  unsigned UseClass = UseMI->getDesc().getSchedClass();
  return ItinData->getOperandLatency(DefClass, DefIdx, UseClass, UseIdx);
}

int
TargetInstrInfo::getOperandLatency(const InstrItineraryData *ItinData,
                                   SDNode *DefNode, unsigned DefIdx,
                                   SDNode *UseNode, unsigned UseIdx) const {
  if (!ItinData || ItinData->isEmpty())
    return -1;

  if (!DefNode->isMachineOpcode())
    return -1;

  unsigned DefClass = get(DefNode->getMachineOpcode()).getSchedClass();
  if (!UseNode->isMachineOpcode())
    return ItinData->getOperandCycle(DefClass, DefIdx);
  unsigned UseClass = get(UseNode->getMachineOpcode()).getSchedClass();
  return ItinData->getOperandLatency(DefClass, DefIdx, UseClass, UseIdx);
}


/// insertNoop - Insert a noop into the instruction stream at the specified
/// point.
void TargetInstrInfo::insertNoop(MachineBasicBlock &MBB, 
                                 MachineBasicBlock::iterator MI) const {
  llvm_unreachable("Target didn't implement insertNoop!");
}


bool TargetInstrInfo::isUnpredicatedTerminator(const MachineInstr *MI) const {
  const TargetInstrDesc &TID = MI->getDesc();
  if (!TID.isTerminator()) return false;
  
  // Conditional branch is a special case.
  if (TID.isBranch() && !TID.isBarrier())
    return true;
  if (!TID.isPredicable())
    return true;
  return !isPredicated(MI);
}


/// Measure the specified inline asm to determine an approximation of its
/// length.
/// Comments (which run till the next SeparatorChar or newline) do not
/// count as an instruction.
/// Any other non-whitespace text is considered an instruction, with
/// multiple instructions separated by SeparatorChar or newlines.
/// Variable-length instructions are not handled here; this function
/// may be overloaded in the target code to do that.
unsigned TargetInstrInfo::getInlineAsmLength(const char *Str,
                                             const MCAsmInfo &MAI) const {
  
  
  // Count the number of instructions in the asm.
  bool atInsnStart = true;
  unsigned Length = 0;
  for (; *Str; ++Str) {
    if (*Str == '\n' || *Str == MAI.getSeparatorChar())
      atInsnStart = true;
    if (atInsnStart && !isspace(*Str)) {
      Length += MAI.getMaxInstLength();
      atInsnStart = false;
    }
    if (atInsnStart && strncmp(Str, MAI.getCommentString(),
                               strlen(MAI.getCommentString())) == 0)
      atInsnStart = false;
  }
  
  return Length;
}
