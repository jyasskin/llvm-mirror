set(LLVM_TARGET_DEFINITIONS PTX.td)

tablegen(PTXGenAsmWriter.inc -gen-asm-writer)
tablegen(PTXGenDAGISel.inc -gen-dag-isel)
tablegen(PTXGenInstrInfo.inc -gen-instr-desc)
tablegen(PTXGenInstrNames.inc -gen-instr-enums)
tablegen(PTXGenRegisterInfo.inc -gen-register-desc)
tablegen(PTXGenRegisterInfo.h.inc -gen-register-desc-header)
tablegen(PTXGenRegisterNames.inc -gen-register-enums)
tablegen(PTXGenSubtarget.inc -gen-subtarget)

add_llvm_target(PTXCodeGen
  PTXISelDAGToDAG.cpp
  PTXISelLowering.cpp
  PTXInstrInfo.cpp
  PTXMCAsmInfo.cpp
  PTXRegisterInfo.cpp
  PTXSubtarget.cpp
  PTXTargetMachine.cpp
  )
