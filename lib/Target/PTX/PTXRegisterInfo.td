//===- PTXRegisterInfo.td - PTX Register defs ----------------*- tblgen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the PTX register file
//===----------------------------------------------------------------------===//

class PTXReg<string n> : Register<n> {
  let Namespace = "PTX";
}

//===----------------------------------------------------------------------===//
//  Registers
//===----------------------------------------------------------------------===//

def P0  : PTXReg<"p0">;
def P1  : PTXReg<"p1">;
def P2  : PTXReg<"p2">;
def P3  : PTXReg<"p3">;
def P4  : PTXReg<"p4">;
def P5  : PTXReg<"p5">;
def P6  : PTXReg<"p6">;
def P7  : PTXReg<"p7">;
def P8  : PTXReg<"p8">;
def P9  : PTXReg<"p9">;
def P10 : PTXReg<"p10">;
def P11 : PTXReg<"p11">;
def P12 : PTXReg<"p12">;
def P13 : PTXReg<"p13">;
def P14 : PTXReg<"p14">;
def P15 : PTXReg<"p15">;
def P16 : PTXReg<"p16">;
def P17 : PTXReg<"p17">;
def P18 : PTXReg<"p18">;
def P19 : PTXReg<"p19">;
def P20 : PTXReg<"p20">;
def P21 : PTXReg<"p21">;
def P22 : PTXReg<"p22">;
def P23 : PTXReg<"p23">;
def P24 : PTXReg<"p24">;
def P25 : PTXReg<"p25">;
def P26 : PTXReg<"p26">;
def P27 : PTXReg<"p27">;
def P28 : PTXReg<"p28">;
def P29 : PTXReg<"p29">;
def P30 : PTXReg<"p30">;
def P31 : PTXReg<"p31">;

//===----------------------------------------------------------------------===//
//  Register classes
//===----------------------------------------------------------------------===//

def Preds : RegisterClass<"PTX", [i1], 8,
                          [P0, P1, P2, P3, P4, P5, P6, P7,
                           P8, P9, P10, P11, P12, P13, P14, P15,
                           P16, P17, P18, P19, P20, P21, P22, P23,
                           P24, P25, P26, P27, P28, P29, P30, P31]>;
