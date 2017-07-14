//===-- HeatCFGPrinter.cpp - CFG printer external interface -----*- C++ -*-===//
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
#include "HeatUtils.h"

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
   bool useHeuristic;
public:
   HeatCFGInfo(Function *F, BlockFrequencyInfo *BFI, uint64_t maxFreq,
               bool useHeuristic){
      this->BFI = BFI;
      this->F = F;
      this->maxFreq = maxFreq;
      this->useHeuristic = useHeuristic;
   }

   BlockFrequencyInfo *getBFI(){ return BFI; }

   Function *getF(){ return this->F; }

   uint64_t getMaxFreq() { return maxFreq; }

   uint64_t getFreq(const BasicBlock *BB){
      return getBlockFreq(BB,BFI,useHeuristic);
   }
};

template <> struct GraphTraits<HeatCFGInfo *> :
  public GraphTraits<const BasicBlock*> {
  static NodeRef getEntryNode(HeatCFGInfo *heatCFG) {
    return &(heatCFG->getF()->getEntryBlock());
  }

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

    std::string Attrs = "";

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
       Attrs = "label=\"W:" + std::to_string(Weight->getZExtValue()) + "\"";
    } else {
       uint64_t total = 0;
       for (unsigned i = 0; i<TI->getNumSuccessors(); i++){
          total += Graph->getFreq(TI->getSuccessor(i));
       }

       unsigned OpNo = I.getSuccessorIndex();

       if (OpNo >= TI->getNumSuccessors())
         return "";

       BasicBlock *SuccBB = TI->getSuccessor(OpNo);

       double val = 0.0;
       if (Graph->getFreq(SuccBB)>0) {
         double freq = Graph->getFreq(SuccBB);
         val = (int(round((freq/double(total))*10000)))/100.0;
       }

       std::stringstream ss;
       ss.precision(2);
       ss << std::fixed << val;
       Attrs = "label=\"" + ss.str() + "%\"";
    }
    return Attrs;
  }

  std::string getNodeAttributes(const BasicBlock *Node, HeatCFGInfo *Graph) {
    uint64_t freq = Graph->getFreq(Node);
    std::string color = getHeatColor(freq, Graph->getMaxFreq());
    std::string edgeColor = (freq<=(Graph->getMaxFreq()/2))?
                            (getHeatColor(0)):(getHeatColor(1));

    std::string attrs = "color=\"" + edgeColor +
                        "ff\", style=filled, fillcolor=\"" + color + "80\"";

    return attrs;
  }  
};

}

static void writeHeatCFGToDotFile(Function &F, BlockFrequencyInfo *BFI,
                           uint64_t maxFreq, bool useHeuristic, bool isSimple) {
  std::string Filename = ("heatcfg." + F.getName() + ".dot").str();
  errs() << "Writing '" << Filename << "'...";

  std::error_code EC;
  raw_fd_ostream File(Filename, EC, sys::fs::F_Text);

  HeatCFGInfo heatCFGInfo(&F,BFI,maxFreq,useHeuristic);

  if (!EC)
     WriteGraph(File, &heatCFGInfo, isSimple);
  else
     errs() << "  error opening file for writing!";
  errs() << "\n";
}

static void writeHeatCFGToDotFile(Module &M,
       function_ref<BlockFrequencyInfo *(Function &)> LookupBFI, bool isSimple){
  uint64_t maxFreq = 0;

  bool useHeuristic = !hasProfiling(M);

  if (!HeatCFGPerFunction)
     maxFreq = getMaxFreq(M,LookupBFI,useHeuristic);

  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (HeatCFGPerFunction)
       maxFreq = getMaxFreq(F,LookupBFI(F),useHeuristic);
    writeHeatCFGToDotFile(F,LookupBFI(F),maxFreq,useHeuristic,isSimple);
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
    "Print heat map of CFG of function to 'dot' file (with no function bodies)",
    false, false);


