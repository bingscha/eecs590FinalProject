#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/Format.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"

using namespace llvm;

namespace {
  struct StatisticsPass : public FunctionPass {
    static char ID;
    StatisticsPass() : FunctionPass(ID) {}

    void getAnalysisUsage(AnalysisUsage &AU) const { 
        AU.addRequired<BlockFrequencyInfoWrapperPass>();
        AU.addRequired<BranchProbabilityInfoWrapperPass>();
        AU.setPreservesAll();
    }

    virtual bool runOnFunction(Function &F) {
        // Get Analysis Pass Information
        BlockFrequencyInfo& bfi = getAnalysis<BlockFrequencyInfoWrapperPass>().getBFI();
        BranchProbabilityInfo& bpi = getAnalysis<BranchProbabilityInfoWrapperPass>().getBPI();

        // Counts
        uint64_t dynamic = 0;
        double int_alu = 0;
        double flo_alu = 0;
        double mem_acc = 0;
        double unb_bra = 0;
        double bia_bra = 0;
        double oth_ins = 0;

        for (BasicBlock& bb : F) {
            uint64_t freq = bfi.getBlockProfileCount(&bb).getValue();
            if (freq <= 0) {
                continue;
            }
            for (Instruction& i : bb) {
                dynamic += freq;
                switch (i.getOpcode()) {
                
                // Integer ALU
                case Instruction::Add:
                case Instruction::Sub:
                case Instruction::Mul:
                case Instruction::UDiv:
                case Instruction::SDiv:
                case Instruction::URem:
                case Instruction::Shl:
                case Instruction::LShr:
                case Instruction::AShr:
                case Instruction::And:
                case Instruction::Or:
                case Instruction::Xor:
                case Instruction::ICmp:
                case Instruction::SRem:
                    int_alu += freq;
                    break;

                // Floating point ALU
                case Instruction::FAdd:
                case Instruction::FSub:
                case Instruction::FMul:
                case Instruction::FDiv:
                case Instruction::FRem:
                case Instruction::FCmp:
                    flo_alu += freq;
                    break;

                // Memory
                case Instruction::Alloca:
                case Instruction::Load:
                case Instruction::Store:
                case Instruction::GetElementPtr:
                case Instruction::Fence:
                case Instruction::AtomicCmpXchg:
                case Instruction::AtomicRMW:
                    mem_acc += freq;
                    break;
                
                case Instruction::Br:
                case Instruction::Switch:
                case Instruction::IndirectBr:
                    unb_bra += freq;
                    for (BasicBlock& dst : F) {
                        if (bpi.getEdgeProbability(&bb, &dst) > BranchProbability(4, 5)) {
                            unb_bra -= freq;
                            bia_bra += freq;
                            break;
                        }
                    }

                    break;

                default:
                    oth_ins += freq;
                    break;
                }
            }
        }

        errs() << F.getName();
        errs() << ", " << dynamic;

        // Don't divide by 0, just set it to 1 and let all percent be 0
        dynamic = (dynamic == 0) ? 1 : dynamic;

        errs() << ", " << format("%f", int_alu / dynamic);
        errs() << ", " << format("%f", flo_alu / dynamic);
        errs() << ", " << format("%f", mem_acc / dynamic);
        errs() << ", " << format("%f", bia_bra / dynamic);
        errs() << ", " << format("%f", unb_bra / dynamic);
        errs() << ", " << format("%f", oth_ins / dynamic);
        errs() << "\n";

        return false;
    }
  };
}

char StatisticsPass::ID = 0;

static RegisterPass<StatisticsPass> X("StatisticsPass", "Statistics Pass");

static void registerStatisticsPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
    PM.add(new BranchProbabilityInfoWrapperPass());
    PM.add(new BlockFrequencyInfoWrapperPass());                        
    PM.add(new StatisticsPass());
}
static RegisterStandardPasses
  RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                 registerStatisticsPass);
