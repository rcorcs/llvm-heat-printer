//===-- HeatCallPrinter.h - CFG printer external interface ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a 'dot-heat-cfg' analysis pass, which emits the
// cfg.<fnname>.dot file for each function in the program, with a graph of the
// CFG for that function coloured with heat map depending on the basic block
// frequency.
//
// This file defines external functions that can be called to explicitly
// instantiate the CFG printer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_HEATCALLPRINTER_H
#define LLVM_ANALYSIS_HEATCALLPRINTER_H

#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace {

class HeatCallGraphDOTPrinterPass : public ModulePass {
public:
  static char ID;
  HeatCallGraphDOTPrinterPass() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const;
  bool runOnModule(Module &M) override;
};

}

#endif
