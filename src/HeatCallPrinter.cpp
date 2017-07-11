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

#include "HeatCallPrinter.h"

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

/*
static cl::opt<bool>
HeatCFGPerFunction("heat-cfg-per-function", cl::init(false), cl::Hidden,
                   cl::desc("Heat CFG per function"));
*/

namespace llvm{

static uint64_t getMaxFreq(Function &F, function_ref<BlockFrequencyInfo *(Function &)> LookupBFI){
  uint64_t maxFreq = 0;
  auto *BFI = LookupBFI(F);
  for(BasicBlock &BB : F){
     uint64_t freqVal = 0;
     Optional< uint64_t > freq = BFI->getBlockProfileCount(&BB);
     if(freq.hasValue()){
        freqVal = freq.getValue();
     }else {
        freqVal = BFI->getBlockFreq(&BB).getFrequency();
     }
     if(freqVal>=maxFreq) maxFreq = freqVal;
  }
  return maxFreq;
}


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
       uint64_t localMaxFreq = ::getMaxFreq(F,LookupBFI);
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

    static const unsigned heatSize = 100;
    static std::string heatPalette[100] = {"#3d50c3", "#4055c8", "#4358cb", "#465ecf", "#4961d2", "#4c66d6", "#4f69d9", "#536edd", "#5572df", "#5977e3", "#5b7ae5", "#5f7fe8", "#6282ea", "#6687ed", "#6a8bef", "#6c8ff1", "#7093f3", "#7396f5", "#779af7", "#7a9df8", "#7ea1fa", "#81a4fb", "#85a8fc", "#88abfd", "#8caffe", "#8fb1fe", "#93b5fe", "#96b7ff", "#9abbff", "#9ebeff", "#a1c0ff", "#a5c3fe", "#a7c5fe", "#abc8fd", "#aec9fc", "#b2ccfb", "#b5cdfa", "#b9d0f9", "#bbd1f8", "#bfd3f6", "#c1d4f4", "#c5d6f2", "#c7d7f0", "#cbd8ee", "#cedaeb", "#d1dae9", "#d4dbe6", "#d6dce4", "#d9dce1", "#dbdcde", "#dedcdb", "#e0dbd8", "#e3d9d3", "#e5d8d1", "#e8d6cc", "#ead5c9", "#ecd3c5", "#eed0c0", "#efcebd", "#f1ccb8", "#f2cab5", "#f3c7b1", "#f4c5ad", "#f5c1a9", "#f6bfa6", "#f7bca1", "#f7b99e", "#f7b599", "#f7b396", "#f7af91", "#f7ac8e", "#f7a889", "#f6a385", "#f5a081", "#f59c7d", "#f4987a", "#f39475", "#f29072", "#f08b6e", "#ef886b", "#ed8366", "#ec7f63", "#e97a5f", "#e8765c", "#e57058", "#e36c55", "#e16751", "#de614d", "#dc5d4a", "#d85646", "#d65244", "#d24b40", "#d0473d", "#cc403a", "#ca3b37", "#c53334", "#c32e31", "#be242e", "#bb1b2c", "#b70d28"};

    uint64_t freqVal = Graph->getFreq(F);
    
    unsigned colorId = unsigned((double(freqVal)/Graph->getMaxFreq())*(heatSize-1));
    std::string color = heatPalette[unsigned((double(freqVal)/Graph->getMaxFreq())*(heatSize-1))];
    std::string edgeColor = ((colorId<(heatSize/2))?heatPalette[0]:heatPalette[heatSize-1]);


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
