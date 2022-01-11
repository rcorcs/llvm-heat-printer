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
#include <set>

using namespace llvm;


static cl::opt<bool>
EstimateEdgeWeight("heat-callgraph-estimate-weight", cl::init(false),
                   cl::Hidden, cl::desc("Estimate edge weights"));

static cl::opt<bool>
FullCallGraph("heat-callgraph-full", cl::init(false), cl::Hidden,
                   cl::desc("Print full call-graph (using external nodes)"));

static cl::opt<bool>
UseCallCounter("heat-callgraph-call-count", cl::init(false), cl::Hidden,
                   cl::desc("Use function's call counter as a heat metric"));


namespace llvm{

class HeatCallGraphInfo {
private:
   CallGraph *CG;
   Module *M;
   std::map<const Function *, uint64_t> freq;
   uint64_t maxFreq;
public:
   std::function<BlockFrequencyInfo *(Function &)> LookupBFI;

   HeatCallGraphInfo(Module *M, CallGraph *CG,
                     function_ref<BlockFrequencyInfo *(Function &)> LookupBFI){
     this->M = M;
     this->CG = CG;
     maxFreq = 0;

     bool useHeuristic = !hasProfiling(*M);

     for(Function &F : *M){
       freq[&F] = 0;
       if(F.isDeclaration())
         continue;
       uint64_t localMaxFreq = 0;
       if (UseCallCounter) {
         Optional< uint64_t > freq = F.getEntryCount().getCount();
         if (freq.hasValue())
           localMaxFreq = freq.getValue();       
       } else {
          localMaxFreq = llvm::getMaxFreq(F,LookupBFI(F),useHeuristic);
       }
       if(localMaxFreq>=maxFreq) maxFreq = localMaxFreq;
       freq[&F] = localMaxFreq;
     }
     this->LookupBFI = LookupBFI;
     removeParallelEdges();
   }

   Module *getModule() const { return M; }
   CallGraph *getCallGraph() const { return CG; }

   uint64_t getFreq(const Function *F) { return freq[F]; }

   uint64_t getMaxFreq() { return maxFreq; }

private:
   void removeParallelEdges(){
      for (auto &I : (*CG)) {
         CallGraphNode *Node = I.second.get();
         
         bool foundParallelEdge = true;
         while (foundParallelEdge) {
            std::set<Function *> visited;
            foundParallelEdge = false;
            for (std::vector<CallGraphNode::CallRecord>::iterator CI =
                 Node->begin(); CI!=Node->end(); CI++) {
               if (visited.find(CI->second->getFunction())==visited.end())
                  visited.insert(CI->second->getFunction());
               else {
                  foundParallelEdge = true;
                  Node->removeCallEdge(CI);
                  break;
               }
            }
         }
      }
   }
};


template <>
struct GraphTraits<HeatCallGraphInfo *> : public GraphTraits<
                                            const CallGraphNode *> {
  static NodeRef getEntryNode(HeatCallGraphInfo *HCG) {
    // Start at the external node!
    return HCG->getCallGraph()->getExternalCallingNode();
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

  static std::string getGraphName(HeatCallGraphInfo *Graph) {
    return "Call graph of module "+
           std::string(Graph->getModule()->getModuleIdentifier());
  }

  static bool isNodeHidden(const CallGraphNode *Node, const HeatCallGraphInfo *G) {
    if (FullCallGraph)
       return false;

    if (Node->getFunction())
       return false;

    return true;
  }

  std::string getNodeLabel(const CallGraphNode *Node, HeatCallGraphInfo *Graph){

    if (Node==Graph->getCallGraph()->getExternalCallingNode())
       return "external caller";

    if (Node==Graph->getCallGraph()->getCallsExternalNode())
       return "external callee";

    if (Function *Func = Node->getFunction())
      return Func->getName().str();

    return "external node";
  }

  static const CallGraphNode *CGGetValuePtr(CallGraphNode::CallRecord P) {
    return P.second;
  }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  typedef mapped_iterator<CallGraphNode::const_iterator,
                          decltype(&CGGetValuePtr)>
      nodes_iterator;


  std::string getEdgeAttributes(const CallGraphNode *Node, nodes_iterator I,
                                HeatCallGraphInfo *Graph) {
    if (!EstimateEdgeWeight)
       return "";

    Function *F = Node->getFunction();
    if (F==nullptr || F->isDeclaration())
       return "";

    Function *SuccFunction = (*I)->getFunction();
    if (SuccFunction==nullptr)
       return "";

    uint64_t counter = getNumOfCalls(*F, *SuccFunction, Graph->LookupBFI);
    std::string Attrs = "label=\"" + std::to_string(counter) + "\"";
    return Attrs;
  }

  std::string getNodeAttributes(const CallGraphNode *Node,
                                HeatCallGraphInfo *Graph) {
    Function *F = Node->getFunction();
    if (F==nullptr || F->isDeclaration())
       return "";

    uint64_t freq = Graph->getFreq(F);
    errs() << F->getName() << " " << freq << "\n";
    std::string color = getHeatColor(freq, Graph->getMaxFreq());
    std::string edgeColor = (freq<(Graph->getMaxFreq()/2))?
                            getHeatColor(0):getHeatColor(1);

    std::string attrs = "color=\"" + edgeColor +
                        "ff\", style=filled, fillcolor=\"" + color + "80\"";

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

  std::string Filename = (std::string(M.getModuleIdentifier())+
                         ".heatcallgraph.dot");
  errs() << "Writing '" << Filename << "'...";

  std::error_code EC;
  raw_fd_ostream File(Filename, EC, sys::fs::OF_Text);

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
