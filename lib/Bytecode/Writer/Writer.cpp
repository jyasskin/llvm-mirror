//===-- Writer.cpp - Library for writing VM bytecode files -------*- C++ -*--=//
//
// This library implements the functionality defined in llvm/Bytecode/Writer.h
//
// This library uses the Analysis library to figure out offsets for
// variables in the method tables...
//
// Note that this file uses an unusual technique of outputting all the bytecode
// to a deque of unsigned char's, then copies the deque to an ostream.  The
// reason for this is that we must do "seeking" in the stream to do back-
// patching, and some very important ostreams that we want to support (like
// pipes) do not support seeking.  :( :( :(
//
// The choice of the deque data structure is influenced by the extremely fast
// "append" speed, plus the free "seek"/replace in the middle of the stream. I
// didn't use a vector because the stream could end up very large and copying
// the whole thing to reallocate would be kinda silly.
//
// Note that the performance of this library is not terribly important, because
// it shouldn't be used by JIT type applications... so it is not a huge focus
// at least.  :)
//
//===----------------------------------------------------------------------===//

#include "WriterInternals.h"
#include "llvm/Module.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Method.h"
#include "llvm/BasicBlock.h"
#include "llvm/ConstPoolVals.h"
#include "llvm/SymbolTable.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Support/STLExtras.h"
#include <string.h>
#include <algorithm>

BytecodeWriter::BytecodeWriter(deque<unsigned char> &o, const Module *M) 
  : Out(o), Table(M, false) {

  outputSignature();

  // Emit the top level CLASS block.
  BytecodeBlock ModuleBlock(BytecodeFormat::Module, Out);

  // Output the ID of first "derived" type:
  output_vbr((unsigned)Type::FirstDerivedTyID, Out);
  align32(Out);

  // Output module level constants, including types used by the method protos
  outputConstants(false);

  // The ModuleInfoBlock follows directly after the Module constant pool
  outputModuleInfoBlock(M);

  // Do the whole module now! Process each method at a time...
  for_each(M->begin(), M->end(),
	   bind_obj(this, &BytecodeWriter::processMethod));

  // If needed, output the symbol table for the module...
  if (M->hasSymbolTable())
    outputSymbolTable(*M->getSymbolTable());
}

// TODO: REMOVE
#include "llvm/Assembly/Writer.h"

void BytecodeWriter::outputConstants(bool isMethod) {
  BytecodeBlock CPool(BytecodeFormat::ConstantPool, Out);

  unsigned NumPlanes = Table.getNumPlanes();
  for (unsigned pno = 0; pno < NumPlanes; pno++) {
    const vector<const Value*> &Plane = Table.getPlane(pno);
    if (Plane.empty()) continue;      // Skip empty type planes...

    unsigned ValNo = 0;
    if (isMethod)                     // Don't reemit module constants
      ValNo = Table.getModuleLevel(pno);
    else if (pno == Type::TypeTyID)
      ValNo = Type::FirstDerivedTyID; // Start emitting at the derived types...
    
    // Scan through and ignore method arguments...
    for (; ValNo < Plane.size() && Plane[ValNo]->isMethodArgument(); ValNo++)
      /*empty*/;

    unsigned NC = ValNo;              // Number of constants
    for (; NC < Plane.size() && 
	   (isa<ConstPoolVal>(Plane[NC]) || 
            isa<Type>(Plane[NC])); NC++) /*empty*/;
    NC -= ValNo;                      // Convert from index into count
    if (NC == 0) continue;            // Skip empty type planes...

    // Output type header: [num entries][type id number]
    //
    output_vbr(NC, Out);

    // Output the Type ID Number...
    int Slot = Table.getValSlot(Plane.front()->getType());
    assert (Slot != -1 && "Type in constant pool but not in method!!");
    output_vbr((unsigned)Slot, Out);

    //cout << "Emitting " << NC << " constants of type '" 
    //	 << Plane.front()->getType()->getName() << "' = Slot #" << Slot << endl;

    for (unsigned i = ValNo; i < ValNo+NC; ++i) {
      const Value *V = Plane[i];
      if (const ConstPoolVal *CPV = V->castConstant()) {
	//cerr << "Serializing value: <" << V->getType() << ">: " 
	//     << ((const ConstPoolVal*)V)->getStrValue() << ":" 
	//     << Out.size() << "\n";
	outputConstant(CPV);
      } else {
	const Type *Ty = cast<const Type>(V);
	outputType(Ty);
      }
    }
  }
}

void BytecodeWriter::outputModuleInfoBlock(const Module *M) {
  BytecodeBlock ModuleInfoBlock(BytecodeFormat::ModuleGlobalInfo, Out);
  
  // Output the types for the global variables in the module...
  for (Module::const_giterator I = M->gbegin(), End = M->gend(); I != End;++I) {
    const GlobalVariable *GV = *I;
    int Slot = Table.getValSlot(GV->getType());
    assert(Slot != -1 && "Module global vars is broken!");

    // Fields: bit0 = isConstant, bit1 = hasInitializer, bit2+ = slot#
    unsigned oSlot = ((unsigned)Slot << 2) | (GV->hasInitializer() << 1) | 
                        GV->isConstant();
    output_vbr(oSlot, Out);

    // If we have an initialized, output it now.
    if (GV->hasInitializer()) {
      Slot = Table.getValSlot(GV->getInitializer());
      assert(Slot != -1 && "No slot for global var initializer!");
      output_vbr((unsigned)Slot, Out);
    }
  }
  output_vbr((unsigned)Table.getValSlot(Type::VoidTy), Out);

  // Output the types of the methods in this module...
  for (Module::const_iterator I = M->begin(), End = M->end(); I != End; ++I) {
    int Slot = Table.getValSlot((*I)->getType());
    assert(Slot != -1 && "Module const pool is broken!");
    assert(Slot >= Type::FirstDerivedTyID && "Derived type not in range!");
    output_vbr((unsigned)Slot, Out);
  }
  output_vbr((unsigned)Table.getValSlot(Type::VoidTy), Out);


  align32(Out);
}

void BytecodeWriter::processMethod(const Method *M) {
  BytecodeBlock MethodBlock(BytecodeFormat::Method, Out);

  // Only output the constant pool and other goodies if needed...
  if (!M->isExternal()) {
    // Get slot information about the method...
    Table.incorporateMethod(M);

    // Output information about the constants in the method...
    outputConstants(true);

    // Output basic block nodes...
    for_each(M->begin(), M->end(),
	     bind_obj(this, &BytecodeWriter::processBasicBlock));
    
    // If needed, output the symbol table for the method...
    if (M->hasSymbolTable())
      outputSymbolTable(*M->getSymbolTable());
    
    Table.purgeMethod();
  }
}


void BytecodeWriter::processBasicBlock(const BasicBlock *BB) {
  BytecodeBlock MethodBlock(BytecodeFormat::BasicBlock, Out);
  // Process all the instructions in the bb...
  for_each(BB->begin(), BB->end(),
	   bind_obj(this, &BytecodeWriter::processInstruction));
}

void BytecodeWriter::outputSymbolTable(const SymbolTable &MST) {
  BytecodeBlock MethodBlock(BytecodeFormat::SymbolTable, Out);

  for (SymbolTable::const_iterator TI = MST.begin(); TI != MST.end(); ++TI) {
    SymbolTable::type_const_iterator I = MST.type_begin(TI->first);
    SymbolTable::type_const_iterator End = MST.type_end(TI->first);
    int Slot;
    
    if (I == End) continue;  // Don't mess with an absent type...

    // Symtab block header: [num entries][type id number]
    output_vbr(MST.type_size(TI->first), Out);

    Slot = Table.getValSlot(TI->first);
    assert(Slot != -1 && "Type in symtab, but not in table!");
    output_vbr((unsigned)Slot, Out);

    for (; I != End; ++I) {
      // Symtab entry: [def slot #][name]
      Slot = Table.getValSlot(I->second);
      assert(Slot != -1 && "Value in symtab but has no slot number!!");
      output_vbr((unsigned)Slot, Out);
      output(I->first, Out, false); // Don't force alignment...
    }
  }
}

void WriteBytecodeToFile(const Module *C, ostream &Out) {
  assert(C && "You can't write a null module!!");

  deque<unsigned char> Buffer;

  // This object populates buffer for us...
  BytecodeWriter BCW(Buffer, C);

  // Okay, write the deque out to the ostream now... the deque is not
  // sequential in memory, however, so write out as much as possible in big
  // chunks, until we're done.
  //
  deque<unsigned char>::const_iterator I = Buffer.begin(), E = Buffer.end();
  while (I != E) {                           // Loop until it's all written
    // Scan to see how big this chunk is...
    const unsigned char *ChunkPtr = &*I;
    const unsigned char *LastPtr = ChunkPtr;
    while (I != E) {
      const unsigned char *ThisPtr = &*++I;
      if (LastPtr+1 != ThisPtr) break;// Advanced by more than a byte of memory?
      LastPtr = ThisPtr;
    }
    
    // Write out the chunk...
    Out.write(ChunkPtr, LastPtr-ChunkPtr+(I != E));
  }

  Out.flush();
}
