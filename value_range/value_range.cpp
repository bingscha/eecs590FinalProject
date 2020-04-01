// LLVM Includes
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/Format.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/LoopInfo.h"

// Personal Includes
#include "VariableRange.h"

// STL includes

using namespace llvm;

using std::max;
using std::min;

namespace {

    bool isBBinLoop(const BasicBlock& BB, const LoopInfo& LI) {
        for (Loop* loop : LI) {
            if (loop->contains(&BB)) {
                return true;
            }
        }
        return false;
    }

    struct ValueRangePass : public FunctionPass {
        static char ID;
        ValueRangePass() : FunctionPass(ID) {}

        void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.addRequired<LoopInfoWrapperPass>();
            AU.setPreservesAll();
        }

        virtual bool runOnFunction(Function &F) {
            errs() << F.getName() << "\n";
            LoopInfo& LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
            for (BasicBlock& bb : F) {
                // if (!isBBinLoop(bb, LI)) {
                    errs() << bb;
                // }
                // else {
                    // errs() << "NOT PRINTING\n";
                // }
            }

            return false;
        }
    };
}

char ValueRangePass::ID = 0;

static RegisterPass<ValueRangePass> X("ValueRange", "Value Range Pass");

static void registerValueRangePass(const PassManagerBuilder &,
                            legacy::PassManagerBase &PM) {
    PM.add(new BranchProbabilityInfoWrapperPass());
    PM.add(new BlockFrequencyInfoWrapperPass());                        
    PM.add(new ValueRangePass());
}
static RegisterStandardPasses
  RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                 registerValueRangePass);
