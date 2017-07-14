//===-- HeatCallPrinter.cpp - CFG printer external interface ----*- C++ -*-===//
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

#include "HeatCallPrinter.h"
#include "HeatUtils.h"

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CallGraph.h"

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
#include <map>

using namespace llvm;

namespace llvm{

class HeatCallGraphInfo {
private:
   CallGraph *CG;
   Module *M;
   std::map<const Function *, uint64_t> freq;
   uint64_t maxFreq;

public:
   HeatCallGraphInfo(Module *M, CallGraph *CG, function_ref<BlockFrequencyInfo *(Function &)> LookupBFI){
     this->M = M;
     this->CG = CG;
     maxFreq = 0;
     for(Function &F : *M){
       freq[&F] = 0;
       if(F.isDeclaration())
         continue;
       uint64_t localMaxFreq = llvm::getMaxFreq(F,LookupBFI(F));
       if(localMaxFreq>=maxFreq) maxFreq = localMaxFreq;
       freq[&F] = localMaxFreq;
     }
   }

   Module *getModule() const { return M; }
   CallGraph *getCallGraph() const { return CG; }

   uint64_t getFreq(const Function *F) { return freq[F]; }

   uint64_t getMaxFreq() { return maxFreq; }
};


template <>
struct GraphTraits<HeatCallGraphInfo *> : public GraphTraits<
                                            const CallGraphNode *> {
  static NodeRef getEntryNode(HeatCallGraphInfo *HCG) {
    return HCG->getCallGraph()->getExternalCallingNode(); // Start at the external node!
  }
  typedef std::pair<const Function *const, std::unique_ptr<CallGraphNode>>
      PairTy;
  static const CallGraphNode *CGGetValuePtr(const PairTy &P) {
    return P.second.get();
  }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  typedef mapped_iterator<CallGraph::const_iterator, decltype(&CGGetValuePtr)>
      nodes_iterator;
  static nodes_iterator nodes_begin(HeatCallGraphInfo *HCG) {
    return nodes_iterator(HCG->getCallGraph()->begin(), &CGGetValuePtr);
  }
  static nodes_iterator nodes_end(HeatCallGraphInfo *HCG) {
    return nodes_iterator(HCG->getCallGraph()->end(), &CGGetValuePtr);
  }
};


template<>
struct DOTGraphTraits<HeatCallGraphInfo *> : public DefaultDOTGraphTraits {

  DOTGraphTraits (bool isSimple=false) : DefaultDOTGraphTraits(isSimple) {}

  static std::string getGraphName(HeatCallGraphInfo *Graph) { return "Call graph of module "+std::string(Graph->getModule()->getModuleIdentifier()); }

  std::string getNodeLabel(const CallGraphNode *Node, HeatCallGraphInfo *Graph) {
    if (Function *Func = Node->getFunction())
      return Func->getName();


    return "external node";
  }

  std::string getNodeAttributes(const CallGraphNode *Node, HeatCallGraphInfo *Graph) {
    Function *F = Node->getFunction();
    if(F==nullptr || F->isDeclaration())
       return "";

    uint64_t freq = Graph->getFreq(F);
    std::string color = getHeatColor(freq, Graph->getMaxFreq());
    std::string edgeColor = (freq<(Graph->getMaxFreq()/2))?getHeatColor(0):getHeatColor(1);

    std::string attrs = "color=\"" + edgeColor + "ff\", style=filled, fillcolor=\"" + color + "80\"";

    return attrs;
  }

};

}

namespace {

void HeatCallGraphDOTPrinterPass::getAnalysisUsage(AnalysisUsage &AU) const {
  ModulePass::getAnalysisUsage(AU);
  AU.addRequired<BlockFrequencyInfoWrapperPass>();
  AU.setPreservesAll();
}

bool HeatCallGraphDOTPrinterPass::runOnModule(Module &M) {
  auto LookupBFI = [this](Function &F) {
    return &this->getAnalysis<BlockFrequencyInfoWrapperPass>(F).getBFI();
  };

  std::string Filename = (std::string(M.getModuleIdentifier()) + ".heatcallgraph.dot");
  errs() << "Writing '" << Filename << "'...";

  std::error_code EC;
  raw_fd_ostream File(Filename, EC, sys::fs::F_Text);

  CallGraph CG(M);
  HeatCallGraphInfo heatCFGInfo(&M,&CG,LookupBFI);

  if(!EC)
     WriteGraph(File, &heatCFGInfo);
  else
     errs() << "  error opening file for writing!";
  errs() << "\n";

  return false;
}

}

char HeatCallGraphDOTPrinterPass::ID = 0;
static RegisterPass<HeatCallGraphDOTPrinterPass> X("dot-heat-callgraph",
                      "Print heat map of call graph to 'dot' file.", false, false);
