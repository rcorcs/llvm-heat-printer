//===-- CFGPrinter.h - CFG printer external interface -----------*- C++ -*-===//
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

#include "HeatCFGPrinter.h"

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

using namespace llvm;


static cl::opt<bool>
HeatCFGPerFunction("heat-cfg-per-function", cl::init(false), cl::Hidden,
                   cl::desc("Heat CFG per function"));

static cl::opt<bool>
UseRawEdgeWeight("heat-cfg-raw-weight", cl::init(false), cl::Hidden,
                   cl::desc("Use raw profiling weights"));

static cl::opt<bool>
NoEdgeWeight("heat-cfg-no-weight", cl::init(false), cl::Hidden,
                   cl::desc("No edge labels with weights"));

namespace llvm{

class HeatCFGInfo {
private:
   BlockFrequencyInfo *BFI;
   Function *F;
   uint64_t maxFreq;

public:
   HeatCFGInfo(Function *F, BlockFrequencyInfo *BFI, uint64_t maxFreq){
      this->BFI = BFI;
      this->F = F;
      this->maxFreq = maxFreq;
   }

   BlockFrequencyInfo *getBFI(){ return BFI; }

   Function *getF(){ return this->F; }

   uint64_t getMaxFreq() { return maxFreq; }

   uint64_t getFreq(BasicBlock *BB){
      uint64_t freqVal = 0;
      Optional< uint64_t > freq = BFI->getBlockProfileCount(BB);
      if (freq.hasValue()) {
         freqVal = freq.getValue();
      } else {
         freqVal = BFI->getBlockFreq(BB).getFrequency();
      }
      return freqVal;
   }
};

template <> struct GraphTraits<HeatCFGInfo *> :
  public GraphTraits<const BasicBlock*> {
  static NodeRef getEntryNode(HeatCFGInfo *heatCFG) { return &(heatCFG->getF()->getEntryBlock()); }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator = pointer_iterator<Function::const_iterator>;

  static nodes_iterator nodes_begin(HeatCFGInfo *heatCFG) {
    return nodes_iterator(heatCFG->getF()->begin());
  }

  static nodes_iterator nodes_end(HeatCFGInfo *heatCFG) {
    return nodes_iterator(heatCFG->getF()->end());
  }

  static size_t size(HeatCFGInfo *heatCFG) { return heatCFG->getF()->size(); }
};

template<>
struct DOTGraphTraits<HeatCFGInfo *> : public DefaultDOTGraphTraits {

  DOTGraphTraits (bool isSimple=false) : DefaultDOTGraphTraits(isSimple) {}

  static std::string getGraphName(HeatCFGInfo *heatCFG) {
    return "Heat CFG for '" + heatCFG->getF()->getName().str() + "' function";
  }

  static std::string getSimpleNodeLabel(const BasicBlock *Node,
                                        HeatCFGInfo *) {
    if (!Node->getName().empty())
      return Node->getName().str();

    std::string Str;
    raw_string_ostream OS(Str);

    Node->printAsOperand(OS, false);
    return OS.str();
  }

  static std::string getCompleteNodeLabel(const BasicBlock *Node,
                                          HeatCFGInfo *) {
    enum { MaxColumns = 80 };
    std::string Str;
    raw_string_ostream OS(Str);

    if (Node->getName().empty()) {
      Node->printAsOperand(OS, false);
      OS << ":";
    }

    OS << *Node;
    std::string OutStr = OS.str();
    if (OutStr[0] == '\n') OutStr.erase(OutStr.begin());

    // Process string output to make it nicer...
    unsigned ColNum = 0;
    unsigned LastSpace = 0;
    for (unsigned i = 0; i != OutStr.length(); ++i) {
      if (OutStr[i] == '\n') {                            // Left justify
        OutStr[i] = '\\';
        OutStr.insert(OutStr.begin()+i+1, 'l');
        ColNum = 0;
        LastSpace = 0;
      } else if (OutStr[i] == ';') {                      // Delete comments!
        unsigned Idx = OutStr.find('\n', i+1);            // Find end of line
        OutStr.erase(OutStr.begin()+i, OutStr.begin()+Idx);
        --i;
      } else if (ColNum == MaxColumns) {                  // Wrap lines.
        // Wrap very long names even though we can't find a space.
        if (!LastSpace)
          LastSpace = i;
        OutStr.insert(LastSpace, "\\l...");
        ColNum = i - LastSpace;
        LastSpace = 0;
        i += 3; // The loop will advance 'i' again.
      }
      else
        ++ColNum;
      if (OutStr[i] == ' ')
        LastSpace = i;
    }
    return OutStr;
  }

  std::string getNodeLabel(const BasicBlock *Node,
                           HeatCFGInfo *Graph) {
    if (isSimple())
      return getSimpleNodeLabel(Node, Graph);
    else
      return getCompleteNodeLabel(Node, Graph);
  }

  static std::string getEdgeSourceLabel(const BasicBlock *Node,
                                        succ_const_iterator I) {
    // Label source of conditional branches with "T" or "F"
    if (const BranchInst *BI = dyn_cast<BranchInst>(Node->getTerminator()))
      if (BI->isConditional())
        return (I == succ_begin(Node)) ? "T" : "F";

    // Label source of switch edges with the associated value.
    if (const SwitchInst *SI = dyn_cast<SwitchInst>(Node->getTerminator())) {
      unsigned SuccNo = I.getSuccessorIndex();

      if (SuccNo == 0) return "def";

      std::string Str;
      raw_string_ostream OS(Str);
      auto Case = *SwitchInst::ConstCaseIt::fromSuccessorIndex(SI, SuccNo);
      OS << Case.getCaseValue()->getValue();
      return OS.str();
    }
    return "";
  }

  /// Display the raw branch weights from PGO.
  std::string getEdgeAttributes(const BasicBlock *Node, succ_const_iterator I,
                                HeatCFGInfo *Graph) {

    if (NoEdgeWeight)
      return "";

    const TerminatorInst *TI = Node->getTerminator();
    if (TI->getNumSuccessors() == 1)
      return "";

    if (UseRawEdgeWeight) {
       MDNode *WeightsNode = TI->getMetadata(LLVMContext::MD_prof);
       if (!WeightsNode)
         return "";

       MDString *MDName = cast<MDString>(WeightsNode->getOperand(0));
       if (MDName->getString() != "branch_weights")
         return "";

       unsigned OpNo = I.getSuccessorIndex() + 1;
       if (OpNo >= WeightsNode->getNumOperands())
         return "";
       ConstantInt *Weight =
           mdconst::dyn_extract<ConstantInt>(WeightsNode->getOperand(OpNo));
       if (!Weight)
         return "";

       // Prepend a 'W' to indicate that this is a weight rather than the actual
       // profile count (due to scaling).
       Twine Attrs = "label=\"W:" + Twine(Weight->getZExtValue()) + "\"";
       return Attrs.str();
    } else {
       uint64_t total = 0;
       for (unsigned i = 0; i<TI->getNumSuccessors(); i++){
          total += Graph->getFreq(TI->getSuccessor(i));
       }

       unsigned OpNo = I.getSuccessorIndex();

       if (OpNo >= TI->getNumSuccessors())
         return "";

       double val = (int(round((double(Graph->getFreq(TI->getSuccessor(OpNo)))/double(total))*10000)))/100.0;
       std::stringstream ss;
       ss << val;
       Twine Attrs = "label=\"" + Twine(ss.str()) + "%\"";
       return Attrs.str();
    }
  }

  std::string getNodeAttributes(const BasicBlock *Node, HeatCFGInfo *Graph) {
    auto *BFI = Graph->getBFI();

    static const unsigned heatSize = 100;
    static std::string heatPalette[100] = {"#3d50c3", "#4055c8", "#4358cb", "#465ecf", "#4961d2", "#4c66d6", "#4f69d9", "#536edd", "#5572df", "#5977e3", "#5b7ae5", "#5f7fe8", "#6282ea", "#6687ed", "#6a8bef", "#6c8ff1", "#7093f3", "#7396f5", "#779af7", "#7a9df8", "#7ea1fa", "#81a4fb", "#85a8fc", "#88abfd", "#8caffe", "#8fb1fe", "#93b5fe", "#96b7ff", "#9abbff", "#9ebeff", "#a1c0ff", "#a5c3fe", "#a7c5fe", "#abc8fd", "#aec9fc", "#b2ccfb", "#b5cdfa", "#b9d0f9", "#bbd1f8", "#bfd3f6", "#c1d4f4", "#c5d6f2", "#c7d7f0", "#cbd8ee", "#cedaeb", "#d1dae9", "#d4dbe6", "#d6dce4", "#d9dce1", "#dbdcde", "#dedcdb", "#e0dbd8", "#e3d9d3", "#e5d8d1", "#e8d6cc", "#ead5c9", "#ecd3c5", "#eed0c0", "#efcebd", "#f1ccb8", "#f2cab5", "#f3c7b1", "#f4c5ad", "#f5c1a9", "#f6bfa6", "#f7bca1", "#f7b99e", "#f7b599", "#f7b396", "#f7af91", "#f7ac8e", "#f7a889", "#f6a385", "#f5a081", "#f59c7d", "#f4987a", "#f39475", "#f29072", "#f08b6e", "#ef886b", "#ed8366", "#ec7f63", "#e97a5f", "#e8765c", "#e57058", "#e36c55", "#e16751", "#de614d", "#dc5d4a", "#d85646", "#d65244", "#d24b40", "#d0473d", "#cc403a", "#ca3b37", "#c53334", "#c32e31", "#be242e", "#bb1b2c", "#b70d28"};

    uint64_t freqVal = 0;
    Optional< uint64_t > freq = BFI->getBlockProfileCount(Node);
    if (freq.hasValue()) {
       freqVal = freq.getValue();
    } else {
       freqVal = BFI->getBlockFreq(Node).getFrequency();
    }
    
    unsigned colorId = unsigned((double(freqVal)/Graph->getMaxFreq())*(heatSize-1));
    std::string color = heatPalette[unsigned((double(freqVal)/Graph->getMaxFreq())*(heatSize-1))];
    std::string edgeColor = ((colorId<(heatSize/2))?heatPalette[0]:heatPalette[heatSize-1]);

    std::string attrs = "color=\"" + edgeColor + "ff\", style=filled, fillcolor=\"" + color + "80\"";

    return attrs;
  }  
};

}


static uint64_t getMaxFreq(Function &F, function_ref<BlockFrequencyInfo *(Function &)> LookupBFI){
  uint64_t maxFreq = 0;
  auto *BFI = LookupBFI(F);
  for(BasicBlock &BB : F){
     uint64_t freqVal = 0;
     Optional< uint64_t > freq = BFI->getBlockProfileCount(&BB);
     if (freq.hasValue()) {
        freqVal = freq.getValue();
     } else {
        freqVal = BFI->getBlockFreq(&BB).getFrequency();
     }
     if (freqVal>=maxFreq)
        maxFreq = freqVal;
  }
  return maxFreq;
}


static uint64_t getMaxFreq(Module &M, function_ref<BlockFrequencyInfo *(Function &)> LookupBFI){
  uint64_t maxFreq = 0;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    uint64_t localMaxFreq = getMaxFreq(F,LookupBFI);
    if (localMaxFreq>=maxFreq)
       maxFreq = localMaxFreq;
  }
  return maxFreq;
}

static void writeHeatCFGToDotFile(Function &F, BlockFrequencyInfo *BFI, uint64_t maxFreq, bool isSimple) {
  std::string Filename = ("heatcfg." + F.getName() + ".dot").str();
  errs() << "Writing '" << Filename << "'...";

  std::error_code EC;
  raw_fd_ostream File(Filename, EC, sys::fs::F_Text);

  HeatCFGInfo heatCFGInfo(&F,BFI,maxFreq);

  if (!EC)
     WriteGraph(File, &heatCFGInfo, isSimple);
  else
     errs() << "  error opening file for writing!";
  errs() << "\n";
}

static void writeHeatCFGToDotFile(Module &M, function_ref<BlockFrequencyInfo *(Function &)> LookupBFI, bool isSimple){
  uint64_t maxFreq = 0;
  if (!HeatCFGPerFunction)
     maxFreq = getMaxFreq(M,LookupBFI);
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (HeatCFGPerFunction)
       maxFreq = getMaxFreq(F,LookupBFI);
    writeHeatCFGToDotFile(F,LookupBFI(F),maxFreq,isSimple);
  }
}

namespace {

void HeatCFGPrinterPass::getAnalysisUsage(AnalysisUsage &AU) const {
  ModulePass::getAnalysisUsage(AU);
  AU.addRequired<BlockFrequencyInfoWrapperPass>();
  AU.setPreservesAll();
}

bool HeatCFGPrinterPass::runOnModule(Module &M) {
  auto LookupBFI = [this](Function &F) {
    return &this->getAnalysis<BlockFrequencyInfoWrapperPass>(F).getBFI();
  };
  writeHeatCFGToDotFile(M,LookupBFI,false);
  return false;
}

void HeatCFGOnlyPrinterPass::getAnalysisUsage(AnalysisUsage &AU) const {
  ModulePass::getAnalysisUsage(AU);
  AU.addRequired<BlockFrequencyInfoWrapperPass>();
  AU.setPreservesAll();
}

bool HeatCFGOnlyPrinterPass::runOnModule(Module &M) {
  auto LookupBFI = [this](Function &F) {
    return &this->getAnalysis<BlockFrequencyInfoWrapperPass>(F).getBFI();
  };
  writeHeatCFGToDotFile(M,LookupBFI,true);
  return false;
}

}

char HeatCFGPrinterPass::ID = 0;
static RegisterPass<HeatCFGPrinterPass> X("dot-heat-cfg",
                      "Print heat map of CFG of function to 'dot' file", false, false);

char HeatCFGOnlyPrinterPass::ID = 0;
static RegisterPass<HeatCFGOnlyPrinterPass> XOnly("dot-heat-cfg-only",
                      "Print heat map of CFG of function to 'dot' file (with no function bodies)", false, false);


