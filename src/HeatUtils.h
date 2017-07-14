//===-- CFGPrinter.h - CFG printer external interface -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_HEATUTILS_H
#define LLVM_ANALYSIS_HEATUTILS_H

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

#include <string>

using namespace llvm;

namespace llvm {

uint64_t getBlockFreq(const BasicBlock *BB, BlockFrequencyInfo *BFI);

uint64_t getMaxFreq(Function &F, BlockFrequencyInfo *BFI);

uint64_t getMaxFreq(Module &M, function_ref<BlockFrequencyInfo *(Function &)> LookupBFI);

std::string getHeatColor(uint64_t freq, uint64_t maxFreq);

std::string getHeatColor(double percent);

}

#endif
