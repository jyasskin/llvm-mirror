//===-- MachineSink.cpp - Sinking for machine instructions ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass moves instructions into successor blocks when possible, so that
// they aren't executed on paths where their results aren't needed.
//
// This pass is not intended to be a replacement or a complete alternative
// for an LLVM-IR-level sinking pass. It is only designed to sink simple
// constructs that are not exposed before lowering and instruction selection.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "machine-sink"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

static cl::opt<bool> 
SplitEdges("machine-sink-split",
           cl::desc("Split critical edges during machine sinking"),
           cl::init(true), cl::Hidden);

STATISTIC(NumSunk,      "Number of machine instructions sunk");
STATISTIC(NumSplit,     "Number of critical edges split");
STATISTIC(NumCoalesces, "Number of copies coalesced");

namespace {
  class MachineSinking : public MachineFunctionPass {
    const TargetInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    MachineRegisterInfo  *MRI;  // Machine register information
    MachineDominatorTree *DT;   // Machine dominator tree
    MachineLoopInfo *LI;
    AliasAnalysis *AA;
    BitVector AllocatableSet;   // Which physregs are allocatable?

    // Remember which edges have been considered for breaking.
    SmallSet<std::pair<MachineBasicBlock*,MachineBasicBlock*>, 8>
    CEBCandidates;

  public:
    static char ID; // Pass identification
    MachineSinking() : MachineFunctionPass(ID) {}

    virtual bool runOnMachineFunction(MachineFunction &MF);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesCFG();
      MachineFunctionPass::getAnalysisUsage(AU);
      AU.addRequired<AliasAnalysis>();
      AU.addRequired<MachineDominatorTree>();
      AU.addRequired<MachineLoopInfo>();
      AU.addPreserved<MachineDominatorTree>();
      AU.addPreserved<MachineLoopInfo>();
    }

    virtual void releaseMemory() {
      CEBCandidates.clear();
    }

  private:
    bool ProcessBlock(MachineBasicBlock &MBB);
    bool isWorthBreakingCriticalEdge(MachineInstr *MI,
                                     MachineBasicBlock *From,
                                     MachineBasicBlock *To);
    MachineBasicBlock *SplitCriticalEdge(MachineInstr *MI,
                                         MachineBasicBlock *From,
                                         MachineBasicBlock *To,
                                         bool BreakPHIEdge);
    bool SinkInstruction(MachineInstr *MI, bool &SawStore);
    bool AllUsesDominatedByBlock(unsigned Reg, MachineBasicBlock *MBB,
                                 MachineBasicBlock *DefMBB,
                                 bool &BreakPHIEdge, bool &LocalUse) const;
    bool PerformTrivialForwardCoalescing(MachineInstr *MI,
                                         MachineBasicBlock *MBB);
  };
} // end anonymous namespace

char MachineSinking::ID = 0;
INITIALIZE_PASS_BEGIN(MachineSinking, "machine-sink",
                "Machine code sinking", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_AG_DEPENDENCY(AliasAnalysis)
INITIALIZE_PASS_END(MachineSinking, "machine-sink",
                "Machine code sinking", false, false)

FunctionPass *llvm::createMachineSinkingPass() { return new MachineSinking(); }

bool MachineSinking::PerformTrivialForwardCoalescing(MachineInstr *MI,
                                                     MachineBasicBlock *MBB) {
  if (!MI->isCopy())
    return false;

  unsigned SrcReg = MI->getOperand(1).getReg();
  unsigned DstReg = MI->getOperand(0).getReg();
  if (!TargetRegisterInfo::isVirtualRegister(SrcReg) ||
      !TargetRegisterInfo::isVirtualRegister(DstReg) ||
      !MRI->hasOneNonDBGUse(SrcReg))
    return false;

  const TargetRegisterClass *SRC = MRI->getRegClass(SrcReg);
  const TargetRegisterClass *DRC = MRI->getRegClass(DstReg);
  if (SRC != DRC)
    return false;

  MachineInstr *DefMI = MRI->getVRegDef(SrcReg);
  if (DefMI->isCopyLike())
    return false;
  DEBUG(dbgs() << "Coalescing: " << *DefMI);
  DEBUG(dbgs() << "*** to: " << *MI);
  MRI->replaceRegWith(DstReg, SrcReg);
  MI->eraseFromParent();
  ++NumCoalesces;
  return true;
}

/// AllUsesDominatedByBlock - Return true if all uses of the specified register
/// occur in blocks dominated by the specified block. If any use is in the
/// definition block, then return false since it is never legal to move def
/// after uses.
bool
MachineSinking::AllUsesDominatedByBlock(unsigned Reg,
                                        MachineBasicBlock *MBB,
                                        MachineBasicBlock *DefMBB,
                                        bool &BreakPHIEdge,
                                        bool &LocalUse) const {
  assert(TargetRegisterInfo::isVirtualRegister(Reg) &&
         "Only makes sense for vregs");

  if (MRI->use_nodbg_empty(Reg))
    return true;

  // Ignoring debug uses is necessary so debug info doesn't affect the code.
  // This may leave a referencing dbg_value in the original block, before
  // the definition of the vreg.  Dwarf generator handles this although the
  // user might not get the right info at runtime.

  // BreakPHIEdge is true if all the uses are in the successor MBB being sunken
  // into and they are all PHI nodes. In this case, machine-sink must break
  // the critical edge first. e.g.
  //
  // BB#1: derived from LLVM BB %bb4.preheader
  //   Predecessors according to CFG: BB#0
  //     ...
  //     %reg16385<def> = DEC64_32r %reg16437, %EFLAGS<imp-def,dead>
  //     ...
  //     JE_4 <BB#37>, %EFLAGS<imp-use>
  //   Successors according to CFG: BB#37 BB#2
  //
  // BB#2: derived from LLVM BB %bb.nph
  //   Predecessors according to CFG: BB#0 BB#1
  //     %reg16386<def> = PHI %reg16434, <BB#0>, %reg16385, <BB#1>
  BreakPHIEdge = true;
  for (MachineRegisterInfo::use_nodbg_iterator
         I = MRI->use_nodbg_begin(Reg), E = MRI->use_nodbg_end();
       I != E; ++I) {
    MachineInstr *UseInst = &*I;
    MachineBasicBlock *UseBlock = UseInst->getParent();
    if (!(UseBlock == MBB && UseInst->isPHI() &&
          UseInst->getOperand(I.getOperandNo()+1).getMBB() == DefMBB)) {
      BreakPHIEdge = false;
      break;
    }
  }
  if (BreakPHIEdge)
    return true;

  for (MachineRegisterInfo::use_nodbg_iterator
         I = MRI->use_nodbg_begin(Reg), E = MRI->use_nodbg_end();
       I != E; ++I) {
    // Determine the block of the use.
    MachineInstr *UseInst = &*I;
    MachineBasicBlock *UseBlock = UseInst->getParent();
    if (UseInst->isPHI()) {
      // PHI nodes use the operand in the predecessor block, not the block with
      // the PHI.
      UseBlock = UseInst->getOperand(I.getOperandNo()+1).getMBB();
    } else if (UseBlock == DefMBB) {
      LocalUse = true;
      return false;
    }

    // Check that it dominates.
    if (!DT->dominates(MBB, UseBlock))
      return false;
  }

  return true;
}

bool MachineSinking::runOnMachineFunction(MachineFunction &MF) {
  DEBUG(dbgs() << "******** Machine Sinking ********\n");

  const TargetMachine &TM = MF.getTarget();
  TII = TM.getInstrInfo();
  TRI = TM.getRegisterInfo();
  MRI = &MF.getRegInfo();
  DT = &getAnalysis<MachineDominatorTree>();
  LI = &getAnalysis<MachineLoopInfo>();
  AA = &getAnalysis<AliasAnalysis>();
  AllocatableSet = TRI->getAllocatableSet(MF);

  bool EverMadeChange = false;

  while (1) {
    bool MadeChange = false;

    // Process all basic blocks.
    CEBCandidates.clear();
    for (MachineFunction::iterator I = MF.begin(), E = MF.end();
         I != E; ++I)
      MadeChange |= ProcessBlock(*I);

    // If this iteration over the code changed anything, keep iterating.
    if (!MadeChange) break;
    EverMadeChange = true;
  }
  return EverMadeChange;
}

bool MachineSinking::ProcessBlock(MachineBasicBlock &MBB) {
  // Can't sink anything out of a block that has less than two successors.
  if (MBB.succ_size() <= 1 || MBB.empty()) return false;

  // Don't bother sinking code out of unreachable blocks. In addition to being
  // unprofitable, it can also lead to infinite looping, because in an
  // unreachable loop there may be nowhere to stop.
  if (!DT->isReachableFromEntry(&MBB)) return false;

  bool MadeChange = false;

  // Walk the basic block bottom-up.  Remember if we saw a store.
  MachineBasicBlock::iterator I = MBB.end();
  --I;
  bool ProcessedBegin, SawStore = false;
  do {
    MachineInstr *MI = I;  // The instruction to sink.

    // Predecrement I (if it's not begin) so that it isn't invalidated by
    // sinking.
    ProcessedBegin = I == MBB.begin();
    if (!ProcessedBegin)
      --I;

    if (MI->isDebugValue())
      continue;

    if (PerformTrivialForwardCoalescing(MI, &MBB))
      continue;

    if (SinkInstruction(MI, SawStore))
      ++NumSunk, MadeChange = true;

    // If we just processed the first instruction in the block, we're done.
  } while (!ProcessedBegin);

  return MadeChange;
}

bool MachineSinking::isWorthBreakingCriticalEdge(MachineInstr *MI,
                                                 MachineBasicBlock *From,
                                                 MachineBasicBlock *To) {
  // FIXME: Need much better heuristics.

  // If the pass has already considered breaking this edge (during this pass
  // through the function), then let's go ahead and break it. This means
  // sinking multiple "cheap" instructions into the same block.
  if (!CEBCandidates.insert(std::make_pair(From, To)))
    return true;

  if (!MI->isCopy() && !MI->getDesc().isAsCheapAsAMove())
    return true;

  // MI is cheap, we probably don't want to break the critical edge for it.
  // However, if this would allow some definitions of its source operands
  // to be sunk then it's probably worth it.
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg()) continue;
    unsigned Reg = MO.getReg();
    if (Reg == 0 || !TargetRegisterInfo::isPhysicalRegister(Reg))
      continue;
    if (MRI->hasOneNonDBGUse(Reg))
      return true;
  }

  return false;
}

MachineBasicBlock *MachineSinking::SplitCriticalEdge(MachineInstr *MI,
                                                     MachineBasicBlock *FromBB,
                                                     MachineBasicBlock *ToBB,
                                                     bool BreakPHIEdge) {
  if (!isWorthBreakingCriticalEdge(MI, FromBB, ToBB))
    return 0;

  // Avoid breaking back edge. From == To means backedge for single BB loop.
  if (!SplitEdges || FromBB == ToBB)
    return 0;

  // Check for backedges of more "complex" loops.
  if (LI->getLoopFor(FromBB) == LI->getLoopFor(ToBB) &&
      LI->isLoopHeader(ToBB))
    return 0;

  // It's not always legal to break critical edges and sink the computation
  // to the edge.
  //
  // BB#1:
  // v1024
  // Beq BB#3
  // <fallthrough>
  // BB#2:
  // ... no uses of v1024
  // <fallthrough>
  // BB#3:
  // ...
  //       = v1024
  //
  // If BB#1 -> BB#3 edge is broken and computation of v1024 is inserted:
  //
  // BB#1:
  // ...
  // Bne BB#2
  // BB#4:
  // v1024 =
  // B BB#3
  // BB#2:
  // ... no uses of v1024
  // <fallthrough>
  // BB#3:
  // ...
  //       = v1024
  //
  // This is incorrect since v1024 is not computed along the BB#1->BB#2->BB#3
  // flow. We need to ensure the new basic block where the computation is
  // sunk to dominates all the uses.
  // It's only legal to break critical edge and sink the computation to the
  // new block if all the predecessors of "To", except for "From", are
  // not dominated by "From". Given SSA property, this means these
  // predecessors are dominated by "To".
  //
  // There is no need to do this check if all the uses are PHI nodes. PHI
  // sources are only defined on the specific predecessor edges.
  if (!BreakPHIEdge) {
    for (MachineBasicBlock::pred_iterator PI = ToBB->pred_begin(),
           E = ToBB->pred_end(); PI != E; ++PI) {
      if (*PI == FromBB)
        continue;
      if (!DT->dominates(ToBB, *PI))
        return 0;
    }
  }

  return FromBB->SplitCriticalEdge(ToBB, this);
}

static bool AvoidsSinking(MachineInstr *MI, MachineRegisterInfo *MRI) {
  return MI->isInsertSubreg() || MI->isSubregToReg() || MI->isRegSequence();
}

/// SinkInstruction - Determine whether it is safe to sink the specified machine
/// instruction out of its current block into a successor.
bool MachineSinking::SinkInstruction(MachineInstr *MI, bool &SawStore) {
  // Don't sink insert_subreg, subreg_to_reg, reg_sequence. These are meant to
  // be close to the source to make it easier to coalesce.
  if (AvoidsSinking(MI, MRI))
    return false;

  // Check if it's safe to move the instruction.
  if (!MI->isSafeToMove(TII, AA, SawStore))
    return false;

  // FIXME: This should include support for sinking instructions within the
  // block they are currently in to shorten the live ranges.  We often get
  // instructions sunk into the top of a large block, but it would be better to
  // also sink them down before their first use in the block.  This xform has to
  // be careful not to *increase* register pressure though, e.g. sinking
  // "x = y + z" down if it kills y and z would increase the live ranges of y
  // and z and only shrink the live range of x.

  // Loop over all the operands of the specified instruction.  If there is
  // anything we can't handle, bail out.
  MachineBasicBlock *ParentBlock = MI->getParent();

  // SuccToSinkTo - This is the successor to sink this instruction to, once we
  // decide.
  MachineBasicBlock *SuccToSinkTo = 0;

  bool BreakPHIEdge = false;
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg()) continue;  // Ignore non-register operands.

    unsigned Reg = MO.getReg();
    if (Reg == 0) continue;

    if (TargetRegisterInfo::isPhysicalRegister(Reg)) {
      if (MO.isUse()) {
        // If the physreg has no defs anywhere, it's just an ambient register
        // and we can freely move its uses. Alternatively, if it's allocatable,
        // it could get allocated to something with a def during allocation.
        if (!MRI->def_empty(Reg))
          return false;

        if (AllocatableSet.test(Reg))
          return false;

        // Check for a def among the register's aliases too.
        for (const unsigned *Alias = TRI->getAliasSet(Reg); *Alias; ++Alias) {
          unsigned AliasReg = *Alias;
          if (!MRI->def_empty(AliasReg))
            return false;

          if (AllocatableSet.test(AliasReg))
            return false;
        }
      } else if (!MO.isDead()) {
        // A def that isn't dead. We can't move it.
        return false;
      }
    } else {
      // Virtual register uses are always safe to sink.
      if (MO.isUse()) continue;

      // If it's not safe to move defs of the register class, then abort.
      if (!TII->isSafeToMoveRegClassDefs(MRI->getRegClass(Reg)))
        return false;

      // FIXME: This picks a successor to sink into based on having one
      // successor that dominates all the uses.  However, there are cases where
      // sinking can happen but where the sink point isn't a successor.  For
      // example:
      //
      //   x = computation
      //   if () {} else {}
      //   use x
      //
      // the instruction could be sunk over the whole diamond for the
      // if/then/else (or loop, etc), allowing it to be sunk into other blocks
      // after that.

      // Virtual register defs can only be sunk if all their uses are in blocks
      // dominated by one of the successors.
      if (SuccToSinkTo) {
        // If a previous operand picked a block to sink to, then this operand
        // must be sinkable to the same block.
        bool LocalUse = false;
        if (!AllUsesDominatedByBlock(Reg, SuccToSinkTo, ParentBlock,
                                     BreakPHIEdge, LocalUse))
          return false;

        continue;
      }

      // Otherwise, we should look at all the successors and decide which one
      // we should sink to.
      for (MachineBasicBlock::succ_iterator SI = ParentBlock->succ_begin(),
           E = ParentBlock->succ_end(); SI != E; ++SI) {
        bool LocalUse = false;
        if (AllUsesDominatedByBlock(Reg, *SI, ParentBlock,
                                    BreakPHIEdge, LocalUse)) {
          SuccToSinkTo = *SI;
          break;
        }
        if (LocalUse)
          // Def is used locally, it's never safe to move this def.
          return false;
      }

      // If we couldn't find a block to sink to, ignore this instruction.
      if (SuccToSinkTo == 0)
        return false;
    }
  }

  // If there are no outputs, it must have side-effects.
  if (SuccToSinkTo == 0)
    return false;

  // It's not safe to sink instructions to EH landing pad. Control flow into
  // landing pad is implicitly defined.
  if (SuccToSinkTo->isLandingPad())
    return false;

  // It is not possible to sink an instruction into its own block.  This can
  // happen with loops.
  if (MI->getParent() == SuccToSinkTo)
    return false;

  // If the instruction to move defines a dead physical register which is live
  // when leaving the basic block, don't move it because it could turn into a
  // "zombie" define of that preg. E.g., EFLAGS. (<rdar://problem/8030636>)
  for (unsigned I = 0, E = MI->getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = MI->getOperand(I);
    if (!MO.isReg()) continue;
    unsigned Reg = MO.getReg();
    if (Reg == 0 || !TargetRegisterInfo::isPhysicalRegister(Reg)) continue;
    if (SuccToSinkTo->isLiveIn(Reg))
      return false;
  }

  DEBUG(dbgs() << "Sink instr " << *MI << "\tinto block " << *SuccToSinkTo);

  // If the block has multiple predecessors, this would introduce computation on
  // a path that it doesn't already exist.  We could split the critical edge,
  // but for now we just punt.
  if (SuccToSinkTo->pred_size() > 1) {
    // We cannot sink a load across a critical edge - there may be stores in
    // other code paths.
    bool TryBreak = false;
    bool store = true;
    if (!MI->isSafeToMove(TII, AA, store)) {
      DEBUG(dbgs() << " *** NOTE: Won't sink load along critical edge.\n");
      TryBreak = true;
    }

    // We don't want to sink across a critical edge if we don't dominate the
    // successor. We could be introducing calculations to new code paths.
    if (!TryBreak && !DT->dominates(ParentBlock, SuccToSinkTo)) {
      DEBUG(dbgs() << " *** NOTE: Critical edge found\n");
      TryBreak = true;
    }

    // Don't sink instructions into a loop.
    if (!TryBreak && LI->isLoopHeader(SuccToSinkTo)) {
      DEBUG(dbgs() << " *** NOTE: Loop header found\n");
      TryBreak = true;
    }

    // Otherwise we are OK with sinking along a critical edge.
    if (!TryBreak)
      DEBUG(dbgs() << "Sinking along critical edge.\n");
    else {
      MachineBasicBlock *NewSucc =
        SplitCriticalEdge(MI, ParentBlock, SuccToSinkTo, BreakPHIEdge);
      if (!NewSucc) {
        DEBUG(dbgs() << " *** PUNTING: Not legal or profitable to "
                        "break critical edge\n");
        return false;
      } else {
        DEBUG(dbgs() << " *** Splitting critical edge:"
              " BB#" << ParentBlock->getNumber()
              << " -- BB#" << NewSucc->getNumber()
              << " -- BB#" << SuccToSinkTo->getNumber() << '\n');
        SuccToSinkTo = NewSucc;
        ++NumSplit;
        BreakPHIEdge = false;
      }
    }
  }

  if (BreakPHIEdge) {
    // BreakPHIEdge is true if all the uses are in the successor MBB being
    // sunken into and they are all PHI nodes. In this case, machine-sink must
    // break the critical edge first.
    MachineBasicBlock *NewSucc = SplitCriticalEdge(MI, ParentBlock,
                                                   SuccToSinkTo, BreakPHIEdge);
    if (!NewSucc) {
      DEBUG(dbgs() << " *** PUNTING: Not legal or profitable to "
            "break critical edge\n");
      return false;
    }

    DEBUG(dbgs() << " *** Splitting critical edge:"
          " BB#" << ParentBlock->getNumber()
          << " -- BB#" << NewSucc->getNumber()
          << " -- BB#" << SuccToSinkTo->getNumber() << '\n');
    SuccToSinkTo = NewSucc;
    ++NumSplit;
  }

  // Determine where to insert into. Skip phi nodes.
  MachineBasicBlock::iterator InsertPos = SuccToSinkTo->begin();
  while (InsertPos != SuccToSinkTo->end() && InsertPos->isPHI())
    ++InsertPos;

  // Move the instruction.
  SuccToSinkTo->splice(InsertPos, ParentBlock, MI,
                       ++MachineBasicBlock::iterator(MI));

  // Conservatively, clear any kill flags, since it's possible that they are no
  // longer correct.
  MI->clearKillInfo();

  return true;
}
