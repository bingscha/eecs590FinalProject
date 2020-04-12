// LLVM Includes
#include "llvm/IR/CFG.h"
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/Format.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DebugInfoMetadata.h"

// Personal Includes
#include "VariableRange.h"
// #include "Ranges.h"

// STL includes
#include <cassert>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace llvm;

using std::cerr;
using std::max;
using std::min;
using std::pair;
using std::queue;
using std::unordered_map;
using std::unordered_set;
using std::vector;

typedef unordered_map<Value*, VariableRange> Ranges;

#define INT_SIZE 32

namespace {

    bool equal_ranges(Ranges& first, Ranges& second) {
        if (first.size() != second.size()) {
            return false;
        }

        for (auto& range : first) {
            Value* val = range.first;
            VariableRange& varRange = range.second;
            if (!second.count(val)) {
                return false;
            }

            if (second[val].min_value != varRange.min_value || second[val].max_value != varRange.max_value) {
                return false;
            }
        }
        return true;
    }

    bool isBBinLoop(const BasicBlock& BB, const LoopInfo& LI) {
        for (Loop* loop : LI) {
            if (loop->contains(&BB)) {
                return true;
            }
        }
        return false;
    }

    void intersectRanges(Ranges& orig, const Ranges& to_merge) {
        // Union all ranges that exist in to_merge and orig together.
        for (const auto& value_range : to_merge) {
            Value* value = value_range.first;
            const VariableRange& range = value_range.second;
            if (orig.count(value)) {
                orig[value] = unionRange(orig[value], range);
            }
        }

        // Find all values that are in orig but not in to_merge
        vector<Value*> victims;
        for (auto& value_range : orig) {
            Value* value = value_range.first;
            if (!to_merge.count(value)) {
                victims.push_back(value);
            }
        }

        // Remove all vals that are not in to_merge
        for (Value* val : victims) {
            orig.erase(val);
        }
    }

    struct ValueRangePass : public FunctionPass {
        static char ID;
        ValueRangePass() : FunctionPass(ID) {}

        void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.addRequired<LoopInfoWrapperPass>();
            AU.setPreservesAll();
        }

        void createSuccessorMap(Function& F) {
            for (BasicBlock& BB : F) {
                for (BasicBlock* pred : predecessors(&BB)) {
                    bb_succs[pred].push_back(&BB);
                }
            }
        }

        void handleAlloca(Instruction* inst, Ranges& ranges) {
            assert(!ranges.count(inst));
            AllocaInst* alloc = dyn_cast<AllocaInst>(inst);
            if (!alloc->getAllocatedType()->isArrayTy()) {
                ranges[alloc] = {INT_MIN, INT_MAX};
            }
        }

        void handleLoad(Instruction* inst, Ranges& ranges) {
            LoadInst* load = dyn_cast<LoadInst>(inst);
            assert(ranges.count(load->getPointerOperand()));

            // Loading from a pointer, just use the same range.
            ranges[load] = ranges[load->getPointerOperand()];
        }

        void handleStore(Instruction* inst, Ranges& ranges) {
            StoreInst* store = dyn_cast<StoreInst>(inst);
            assert(ranges.count(store->getPointerOperand()));

            if (isa<ConstantInt>(store->getValueOperand())) {
                ConstantInt* constant = dyn_cast<ConstantInt>(store->getValueOperand());
                ranges[store->getPointerOperand()].min_value = static_cast<int>(constant->getSExtValue());
                ranges[store->getPointerOperand()].max_value = static_cast<int>(constant->getSExtValue());
            }
            else { //TODO
                assert(ranges.count(store->getValueOperand()));
                ranges[store->getPointerOperand()] = ranges[store->getValueOperand()];
            }
        }

        void handleBinaryOperations(Instruction* inst, Ranges& ranges, char op) {
            Value* first = inst->getOperand(0);
            Value* second = inst->getOperand(1);

            VariableRange firstRange;
            VariableRange secondRange;

            if (isa<ConstantInt>(first)) {
                ConstantInt* firstConst = dyn_cast<ConstantInt>(first);
                int val = static_cast<int>(firstConst->getSExtValue());
                firstRange.min_value = val;
                firstRange.max_value = val;
            }
            else {
                assert(ranges.count(first));
                firstRange = ranges[first];
            }

            if (isa<ConstantInt>(second)) {
                ConstantInt* secondConst = dyn_cast<ConstantInt>(second);
                int val = static_cast<int>(secondConst->getSExtValue());
                secondRange.min_value = val;
                secondRange.max_value = val;
            }
            else {
                assert(ranges.count(second));
                secondRange = ranges[second];
            }

            switch(op) {
                case '+' :
                    ranges[inst] = addRanges(firstRange, secondRange);
                    break;
                case '-':
                    ranges[inst] = subRanges(firstRange, secondRange);
                    break;
                case '/':
                    ranges[inst] = divRanges(firstRange, secondRange);
                    break;
                case '*':
                    ranges[inst] = multRanges(firstRange, secondRange);
                    break;
                default :
                    // errs() << "Unexpected op\nExitting\n";
                    exit(1); 
            }
        }

        void handleICMP(ICmpInst* icmp, Ranges& if_ranges, Ranges& else_ranges, bool& if_reachable, bool& else_reachable) {
            assert(equal_ranges(if_ranges, else_ranges));

            // May need a boolean to determine if range is valid or not, to be used with merging
            Value* firstVal = icmp->getOperand(0);
            Value* secondVal = icmp->getOperand(1);

            VariableRange first;
            VariableRange second;

            bool firstConst = false;
            bool secondConst = false;

            if (isa<ConstantInt>(firstVal)) {
                ConstantInt* constant = dyn_cast<ConstantInt>(firstVal);
                int val = static_cast<int>(constant->getSExtValue());
                first.min_value = val;
                first.max_value = val;
                firstConst = true;
            }
            else {
                assert(if_ranges.count(firstVal));
                assert(isa<LoadInst>(firstVal));                // TODO: Temporary Fix, we want to only fix the alloca dependent on this instr
                first = if_ranges[firstVal];
            }

            if (isa<ConstantInt>(secondVal)) {
                ConstantInt* constant = dyn_cast<ConstantInt>(secondVal);
                int val = static_cast<int>(constant->getSExtValue());
                second.min_value = val;
                second.max_value = val;
                secondConst = true;
            }
            else {
                assert(if_ranges.count(secondVal));
                assert(isa<LoadInst>(secondVal));               // TODO: Temporary Fix, we want to only fix the alloca dependent on this instr
                second = if_ranges[secondVal];
            }

            VariableRange if_lhs, if_rhs, else_lhs, else_rhs;

            switch (icmp->getSignedPredicate()) {
                case CmpInst::Predicate::ICMP_EQ :
                    if_lhs = equalRange(first, second, if_reachable);
                    if_rhs = if_lhs;
                    else_lhs = first;
                    else_rhs = second;
                    else_reachable = true;
                    break;
                case CmpInst::Predicate::ICMP_NE :
                    if_lhs = first;
                    if_rhs = second;
                    if_reachable = true;
                    else_lhs = equalRange(first, second, else_reachable);
                    else_rhs = else_lhs;
                    break;
                case CmpInst::Predicate::ICMP_SGT :
                    if_lhs = greaterRange(first, second, if_reachable);
                    if (if_reachable) {
                        if_rhs = lessRange(second, if_lhs, if_reachable);
                    }

                    else_lhs = lessEqualRange(first, second, else_reachable);
                    if (else_reachable) {
                        else_rhs = greaterEqualRange(second, else_lhs, else_reachable);
                    }
                    break;
                case CmpInst::Predicate::ICMP_SLT :
                    if_lhs = lessRange(first, second, if_reachable);
                    if (if_reachable) {
                        if_rhs = greaterRange(second, if_lhs, if_reachable);
                    }

                    else_lhs = greaterEqualRange(first, second, else_reachable);
                    if (else_reachable) {
                        else_rhs = lessEqualRange(second, else_lhs, else_reachable);
                    }
                    break;
                case CmpInst::Predicate::ICMP_SGE :
                    if_lhs = greaterEqualRange(first, second, if_reachable);
                    if (if_reachable) {
                        if_rhs = lessEqualRange(second, if_lhs, if_reachable);
                    }

                    else_lhs = lessRange(first, second, else_reachable);
                    if (else_reachable) {
                        else_rhs = greaterRange(second, else_lhs, else_reachable);
                    }
                    break;
                case CmpInst::Predicate::ICMP_SLE :
                    if_lhs = lessEqualRange(first, second, if_reachable);
                    if (if_reachable) {
                        if_rhs = greaterEqualRange(second, if_lhs, if_reachable);
                    }

                    else_lhs = greaterRange(first, second, else_reachable);
                    if (else_reachable) {
                        else_rhs = lessRange(second, else_lhs, else_reachable);
                    }
                    break;
                default:
                    errs() << "Unknown\n";
            }
            
            if (!firstConst) {
                Value* inst = (dyn_cast<LoadInst>(firstVal))->getOperand(0);        // TODO: Temporary Fix, we want to only fix the alloca dependent on this instr
                if_ranges[inst] = if_lhs;
                else_ranges[inst] = else_lhs;
            }

            if (!secondConst) {
                Value* inst = (dyn_cast<LoadInst>(secondVal))->getOperand(0);       // TODO: Temporary Fix, we want to only fix the alloca dependent on this instr
                if_ranges[inst] = if_rhs;
                else_ranges[inst] = else_rhs;
            }
        }

        bool handleBranchInstruction(Instruction* inst, Ranges& ranges) {
            BranchInst* branch = dyn_cast<BranchInst>(inst);

            if (branch->isConditional()) {
                BasicBlock* parent = inst->getParent();
                ICmpInst* icmp = dyn_cast<ICmpInst>(inst->getOperand(0));
                BasicBlock* else_succ = dyn_cast<BasicBlock>(inst->getOperand(1));
                BasicBlock* if_succ = dyn_cast<BasicBlock>(inst->getOperand(2));

                Ranges if_ranges = ranges;
                Ranges else_ranges = ranges;

                bool if_reachable = false;
                bool else_reachable = false;

                handleICMP(icmp, if_ranges, else_ranges, if_reachable, else_reachable);

                bool changed = false;
                bool initialized = false;

                if (if_reachable) {
                    // Update if ranges
                    if (bb_to_succ_ranges.count(parent)) {
                        if (bb_to_succ_ranges[parent].count(if_succ)) {
                            Ranges& previousRange = bb_to_succ_ranges[parent][if_succ];
                            if (!equal_ranges(if_ranges, previousRange)) {
                                bb_to_succ_ranges[parent][if_succ] = if_ranges;
                                changed = true;
                            }
                        }
                        else {
                            bb_to_succ_ranges[parent][if_succ] = if_ranges;
                            changed = true;
                        }
                    }
                    else {
                        bb_to_succ_ranges[parent][if_succ] = if_ranges;
                        changed = true;
                        initialized = true;
                    }
                }

                // Update else ranges
                if (else_reachable) {
                    if (!initialized) {
                        if(bb_to_succ_ranges[parent].count(else_succ)) {
                            Ranges& previousRange = bb_to_succ_ranges[parent][else_succ];
                            if (!equal_ranges(else_ranges, previousRange)) {
                                bb_to_succ_ranges[parent][else_succ] = else_ranges;
                                changed = true;
                            }
                        }
                        else {
                            bb_to_succ_ranges[parent][else_succ] = else_ranges;
                            changed = true;
                        }
                    }
                    else {
                        bb_to_succ_ranges[parent][else_succ] = else_ranges;
                        changed = true;
                    }
                }

                return changed;
            }
            else {
                // errs() << "WE HAVE GOTTEN HERE\n" << *inst << "\n";
                BasicBlock* parent = inst->getParent();
                BasicBlock* succ = dyn_cast<BasicBlock>(inst->getOperand(0));
                if (bb_to_succ_ranges.count(parent)) {
                    assert(bb_to_succ_ranges[parent].count(succ));
                    Ranges& previousRange = bb_to_succ_ranges[parent][succ];
                    if (equal_ranges(ranges, previousRange)) {
                        return false;
                    }
                    else {
                        bb_to_succ_ranges[parent][succ] = ranges;
                        return true;
                    }
                }
                else {
                    bb_to_succ_ranges[parent][succ] = ranges;
                    return true;
                }
                
            }
        }

        void handleGEPOperations(Instruction* inst, Ranges& ranges) {
            // Nothing is assumed about values in arrays
            ranges[inst] = VariableRange();
        }

        void handleCallOperations(Instruction* inst, Ranges& ranges) {
            // Nothing is assumed about calls
            ranges[inst] = VariableRange();
        }

        void handleCastOperations(Instruction* inst, Ranges& ranges) {
            assert(ranges.count(inst->getOperand(0)));
            ranges[inst] = ranges[inst->getOperand(0)];
        }

        bool handleInst(Instruction* inst, Ranges& ranges) {
            switch (inst->getOpcode()) {
                case Instruction::Alloca :
                    handleAlloca(inst, ranges);
                    break;
                case Instruction::Load :
                    handleLoad(inst, ranges);
                    break;
                case Instruction::Store : 
                    handleStore(inst, ranges);
                    break;
                case Instruction::Add :
                    handleBinaryOperations(inst, ranges, '+');
                    break;
                case Instruction::Sub :
                    handleBinaryOperations(inst, ranges, '-');
                    break;
                case Instruction::SDiv :
                    handleBinaryOperations(inst, ranges, '/');
                    break;
                case Instruction::Mul :
                    handleBinaryOperations(inst, ranges, '*');
                    break;
                case Instruction::Br :
                    // Handles branches differently, should just return as it updates
                    return handleBranchInstruction(inst, ranges);
                case Instruction::GetElementPtr :
                    handleGEPOperations(inst, ranges);
                    break;
                case Instruction::Call :
                    handleCallOperations(inst, ranges);
                    break;
                case Instruction::Trunc :
                case Instruction::ZExt :
                case Instruction::SExt :
                case Instruction::FPTrunc:
                case Instruction::FPExt:
                case Instruction::FPToUI:
                case Instruction::FPToSI:
                case Instruction::UIToFP:
                case Instruction::SIToFP:
                case Instruction::IntToPtr:
                case Instruction::PtrToInt:
                case Instruction::BitCast:
                case Instruction::AddrSpaceCast:
                    // Casting instructions, results should be same as input
                    handleCastOperations(inst, ranges);
                    break;
                case Instruction::ICmp : // Ignore this instr, will be handled in branch
                case Instruction::Ret : // Ignore this instruction, nothing is assumed about post-condition
                    break;
                default:
                    errs() << *inst << "\n";
                    break;
            }

            if (inst_to_ranges.count(inst)) {
                if (equal_ranges(ranges, inst_to_ranges[inst])) {
                    return false;
                }
                else {
                    widen(ranges, inst_to_ranges[inst]);
                    inst_to_ranges[inst] = ranges;
                    return true;
                }
            }
            else {
                inst_to_ranges[inst] = ranges;
                return true;
            }
        }

        bool widen(Ranges& current, Ranges& original) {
            bool widened = false;

            for (auto& val_to_var_range : current) {
                Value* val = val_to_var_range.first;
                VariableRange& range = val_to_var_range.second;

                if (original.count(val)) {
                    VariableRange& otherRange = original[val];

                    if (range.max_value > otherRange.max_value) {
                        range.max_value = INT_MAX;
                        widened = true;
                    }

                    if (range.min_value < otherRange.min_value) {
                        range.min_value = INT_MIN;
                        widened = true;
                    }
                }
            }

            return widened;
        }

        void getArrayInformation(Function& F) {
            auto DL = F.getParent()->getDataLayout();
            for (BasicBlock& BB : F) {
                for (Instruction& I : BB) {
                    if (isa<AllocaInst>(&I)) {
                        AllocaInst* alloca = dyn_cast<AllocaInst>(&I);
                        if (alloca->getAllocatedType()->isArrayTy()) {
                            array_sizes[alloca] = alloca->getAllocationSizeInBits(DL).getValue() / INT_SIZE;
                        }
                    }
                }
            }
        }

        Ranges& getBeforeRanges(Instruction* inst) {
            if (&(*(inst->getParent()->begin())) == inst) {
                return basic_block_before_ranges[inst->getParent()];
            }
            else {
                Instruction* prev = nullptr;
                for (Instruction& I : *(inst->getParent())) {
                    if (inst == &I) {
                        break;
                    }
                    prev = &I;
                }

                return inst_to_ranges[prev];
            }
        }

        void printDebugInformation(Instruction* inst) {
            DILocation* loc = inst->getDebugLoc();
            if (!loc) {
                errs() << "WARNING: Possible array out of bounds access at ";
                errs() << *inst << "\n";
                errs() << "Please compile with -g to see line numbers.\n";
            }
            else {
                loc->getDirectory();
                errs() << loc->getFilename() << ":" << loc->getLine() << ":"; 
                errs() << loc->getColumn() << ": warning: possible array out of bounds access.\n";
            }   
        }

        void checkArrayBounds(Function& F) {
            for (BasicBlock& BB : F) {
                for (Instruction& I : BB) {
                    if (isa<GetElementPtrInst>(&I)) {
                        if (!inst_to_ranges.count(&I)) {
                            // We determined this block was not reachable
                            continue;
                        }
                        Ranges& ranges = getBeforeRanges(&I);
                        AllocaInst* array = dyn_cast<AllocaInst>(I.getOperand(0));
                        assert(array_sizes.count(array));
                        int array_size = array_sizes[array];

                        Value* index = I.getOperand(2);
                        VariableRange range;
                        if (isa<ConstantInt>(index)) {
                            ConstantInt* constant = dyn_cast<ConstantInt>(index);
                            range.min_value = static_cast<int>(constant->getSExtValue());
                            range.max_value = range.min_value;
                        }
                        else {
                            range = ranges[index];
                        }

                        if (outOfRange(range, array_size)) {
                            printDebugInformation(&I);
                            // errs() << "WARNING: OUT OF RANGE ARRAY\n" << I << "\n";
                        }
                    }
                }
            }
        }

        virtual bool runOnFunction(Function &F) {
            createSuccessorMap(F);

            // Continually iterate through this until convergence.
            bool changed = true;
            int count = 0;
            while (changed) {
                changed = false;
                BasicBlock& init = F.getEntryBlock();
                queue<BasicBlock*> bfs;
                // errs() << "=========================== Iteration " << count++ << " ===========================\n";
                unordered_set<BasicBlock*> visited;
                bfs.push(&init);
                visited.insert(&init);
                while (bfs.size()) {
                    // Get the next bb
                    BasicBlock* current = bfs.front();
                    bfs.pop();

                    // Push in the successors to the bb
                    for (BasicBlock* succ : bb_succs[current]) {
                        if (!visited.count(succ)) {
                            bfs.push(succ);
                            visited.insert(succ);
                        }
                    }

                    // Create a new range
                    Ranges unioned;
                    
                    bool valid = !current->hasNPredecessorsOrMore(1);

                    // If this basic block has a predecessor, intersect the ranges of them
                    if (!valid) {
                        // Get the first predecessor and set it to unioned
                        bool initialized = false;

                        // Go through all predecessors to update
                        for (BasicBlock* pred : predecessors(current)) {
                            if (bb_to_succ_ranges.count(pred)) {
                                if (bb_to_succ_ranges[pred].count(current)) {
                                    if (!initialized) {
                                        unioned = bb_to_succ_ranges[pred][current];
                                        initialized = true;
                                        valid = true;
                                    }
                                    else {
                                        intersectRanges(unioned, bb_to_succ_ranges[pred][current]);
                                    }
                                }
                            }
                        }
                    }

                    if (!valid) {
                        continue;
                    }

                    // Update the before range appropriately, mark if there is any change.
                    if (basic_block_before_ranges.count(current)) {
                        if (!equal_ranges(basic_block_before_ranges[current], unioned)) {
                            changed = true;
                            basic_block_before_ranges[current] = unioned;
                        }
                    }
                    else {
                        changed = true;
                        basic_block_before_ranges[current] = unioned;
                    }

                    for (Instruction& I : *current) {
                        changed = handleInst(&I, unioned) || changed;
                    }
                            
                }

            }

            // errs() << F << "\n";
            // for (BasicBlock& bb : F) {
            //     errs() << "++++++++++++++++++++++++++++++++ NEW BB ++++++++++++++++++++++++++++++++\n";
            //     for (Instruction& I : bb) {
            //         if (inst_to_ranges.count(&I)) {
            //             errs() << I << "\n";
            //             for (auto& range : inst_to_ranges[&I]) {
            //                 errs() << "\t" << *(range.first) << " " << range.second.min_value << " " << range.second.max_value << "\n";
            //             }
            //         }
            //         else {
            //             errs() << "NOT INCLUDED: " << I << "\n";
            //         }
            //     }
            // }

            /**************************** BEGIN STATIC ARRAY RANGE ANALYSIS ****************************/

            // Get all of the arrays
            getArrayInformation(F);

            // Check to see if range is out of bounds
            checkArrayBounds(F);
            return false;
        }
    private:
        unordered_map<BasicBlock*, vector<BasicBlock*>> bb_succs;
        unordered_map<Instruction*, Ranges> inst_to_ranges;
        unordered_map<BasicBlock*, Ranges> basic_block_before_ranges;
        unordered_map<BasicBlock*, unordered_map<BasicBlock*, Ranges> > bb_to_succ_ranges;
        unordered_set<BasicBlock*> unreachable;
        unordered_map<AllocaInst*, int> array_sizes;
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
