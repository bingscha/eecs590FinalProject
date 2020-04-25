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

// STL includes
#include <cassert>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// usings
using namespace llvm;

using std::cerr;
using std::max;
using std::min;
using std::pair;
using std::queue;
using std::unordered_map;
using std::unordered_set;
using std::vector;

// Definition of a range for an instruction, we map the value to its range
typedef unordered_map<Value*, VariableRange> Ranges;

#define INT_SIZE 32

// LLVM recommends anonymous namespaces
namespace {
    /*
     * Description:
     * Given set of variable ranges, determine if they are equal. Being equal is defined as having
     * the same values stored and all of the values have the same range.
     */
    bool equal_ranges(Ranges& first, Ranges& second) {
        if (first.size() != second.size()) {
            return false;
        }

        // Iterate through all of first and check if it exists in second with the same range
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

    /*
     * Description:
     * Given two set of ranges, merge them. If a value exists in one but not the other, that
     * value is removed. If they exist in both, the resulting range encapsulates both ranges.
     */
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

    /*
     * Description:
     * A function pass that checks the bounds of statically allocated arrays.
     * 
     * Requirements:
     * 1. Only integer variables
     * 2. No dynamically allocated arrays
     * 3. No integer overflow
     * 4. Boolean conditions only depend on variables and constants
     * 5. Binary operators are restricted to +, -, *, /
     */
    struct BoundsCheckPass : public FunctionPass {
        // ================== BEGIN LLVM PASS INFO ================== //
        
        static char ID;
        BoundsCheckPass() : FunctionPass(ID) {}

        // Necessary for LLVM Passes
        void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.setPreservesAll();
        }

        // ================== END LLVM PASS INFO ================== //

        // ================== BEGIN VALUE RANGE ANALYSIS ================== //

        /*
         * Description:
         * Creates a successor map so for each basic block, we know all successors.
         */
        void createSuccessorMap(Function& F) {
            for (BasicBlock& BB : F) {
                for (BasicBlock* pred : predecessors(&BB)) {
                    bb_succs[pred].push_back(&BB);
                }
            }
        }

        /*
         * Description:
         * Initializes value stored at alloca with [INT_MIN, INT_MAX] in ranges
         */
        void handleAlloca(Instruction* inst, Ranges& ranges) {
            assert(!ranges.count(inst));
            AllocaInst* alloc = dyn_cast<AllocaInst>(inst);

            // We ignore arrays, nothing is assumed about their stored values
            if (!alloc->getAllocatedType()->isArrayTy()) {
                ranges[alloc] = {INT_MIN, INT_MAX};
            }
        }

        /*
         * Description:
         * Updates value we are loading to with range we are loading from
         */
        void handleLoad(Instruction* inst, Ranges& ranges) {
            LoadInst* load = dyn_cast<LoadInst>(inst);
            assert(ranges.count(load->getPointerOperand()));

            // Loading from a pointer, just use the same range.
            ranges[load] = ranges[load->getPointerOperand()];
        }

        /*
         * Description:
         * Updates value we are storing to with range we are storing from
         */
        void handleStore(Instruction* inst, Ranges& ranges) {
            StoreInst* store = dyn_cast<StoreInst>(inst);
            assert(ranges.count(store->getPointerOperand()));

            // If it is a constant, c, update the range to be [c, c]. Else, use whatever known range
            if (isa<ConstantInt>(store->getValueOperand())) {
                ConstantInt* constant = dyn_cast<ConstantInt>(store->getValueOperand());
                ranges[store->getPointerOperand()].min_value = static_cast<int>(constant->getSExtValue());
                ranges[store->getPointerOperand()].max_value = static_cast<int>(constant->getSExtValue());
            }
            else {
                assert(ranges.count(store->getValueOperand()));
                ranges[store->getPointerOperand()] = ranges[store->getValueOperand()];
            }
        }

        /*
         * Description:
         * Initializes result of binary op with new calculated range based off of op.
         */
        void handleBinaryOperations(Instruction* inst, Ranges& ranges, char op) {
            Value* first = inst->getOperand(0);
            Value* second = inst->getOperand(1);

            VariableRange firstRange;
            VariableRange secondRange;

            // If it is a constant, c, retrieve range [c,c], else get already stored range.
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

            // Depending on operation, calculate the new range.
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
                    errs() << "ERROR: Unexpected binary operation.\nExitting\n";
                    exit(1); 
            }
        }

        /*
         * Description:
         * Determine the new if_range and else_range depending on if the icmp results in true or false.
         * If it is not possible to reach the specific BB from this BB, we say it is not reachable.
         */
        void handleICMP(ICmpInst* icmp, Ranges& if_ranges, Ranges& else_ranges, bool& if_reachable, bool& else_reachable) {
            assert(equal_ranges(if_ranges, else_ranges));

            Value* firstVal = icmp->getOperand(0);
            Value* secondVal = icmp->getOperand(1);

            VariableRange first;
            VariableRange second;

            bool firstConst = false;
            bool secondConst = false;

            // Determine what the first and second range we are calculating for.
            if (isa<ConstantInt>(firstVal)) {
                ConstantInt* constant = dyn_cast<ConstantInt>(firstVal);
                int val = static_cast<int>(constant->getSExtValue());
                first.min_value = val;
                first.max_value = val;
                firstConst = true;
            }
            else {
                assert(if_ranges.count(firstVal));
                assert(isa<LoadInst>(firstVal));
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
                assert(isa<LoadInst>(secondVal));
                second = if_ranges[secondVal];
            }

            VariableRange if_lhs, if_rhs, else_lhs, else_rhs;

            // Iterate through all predicates to determine new ranges in if and else case
            switch (icmp->getSignedPredicate()) {
                case CmpInst::Predicate::ICMP_EQ : // ==
                    if_lhs = equalRange(first, second, if_reachable);
                    if_rhs = if_lhs;
                    else_lhs = first;
                    else_rhs = second;
                    else_reachable = true;
                    break;
                case CmpInst::Predicate::ICMP_NE : // !=
                    if_lhs = first;
                    if_rhs = second;
                    if_reachable = true;
                    else_lhs = equalRange(first, second, else_reachable);
                    else_rhs = else_lhs;
                    break;
                case CmpInst::Predicate::ICMP_SGT : // >
                    if_lhs = greaterRange(first, second, if_reachable);
                    if (if_reachable) {
                        if_rhs = lessRange(second, if_lhs, if_reachable);
                    }

                    else_lhs = lessEqualRange(first, second, else_reachable);
                    if (else_reachable) {
                        else_rhs = greaterEqualRange(second, else_lhs, else_reachable);
                    }
                    break;
                case CmpInst::Predicate::ICMP_SLT : // <
                    if_lhs = lessRange(first, second, if_reachable);
                    if (if_reachable) {
                        if_rhs = greaterRange(second, if_lhs, if_reachable);
                    }

                    else_lhs = greaterEqualRange(first, second, else_reachable);
                    if (else_reachable) {
                        else_rhs = lessEqualRange(second, else_lhs, else_reachable);
                    }
                    break;
                case CmpInst::Predicate::ICMP_SGE : // >=
                    if_lhs = greaterEqualRange(first, second, if_reachable);
                    if (if_reachable) {
                        if_rhs = lessEqualRange(second, if_lhs, if_reachable);
                    }

                    else_lhs = lessRange(first, second, else_reachable);
                    if (else_reachable) {
                        else_rhs = greaterRange(second, else_lhs, else_reachable);
                    }
                    break;
                case CmpInst::Predicate::ICMP_SLE : // <=
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
                    errs() << "ERROR: Unknown predicate in if statement.\nExitting.\n";
                    exit(1);
            }
            
            // If either ranges were not constant, update the corresponding range
            if (!firstConst) {
                Value* inst = (dyn_cast<LoadInst>(firstVal))->getOperand(0);
                if_ranges[inst] = if_lhs;
                else_ranges[inst] = else_lhs;
            }

            if (!secondConst) {
                Value* inst = (dyn_cast<LoadInst>(secondVal))->getOperand(0);
                if_ranges[inst] = if_rhs;
                else_ranges[inst] = else_rhs;
            }
        }

        /*
         * Description:
         * From the branch instruction, determine what the resulting range is for all successors of the basic block.
         * Returns true if update was made.
         */
        bool handleBranchInstruction(Instruction* inst, Ranges& ranges) {
            BranchInst* branch = dyn_cast<BranchInst>(inst);

            // If it is conditional, determine how the icmp effects the cases.
            if (branch->isConditional()) {
                BasicBlock* parent = inst->getParent();
                ICmpInst* icmp = dyn_cast<ICmpInst>(inst->getOperand(0));
                BasicBlock* else_succ = dyn_cast<BasicBlock>(inst->getOperand(1));
                BasicBlock* if_succ = dyn_cast<BasicBlock>(inst->getOperand(2));

                Ranges if_ranges = ranges;
                Ranges else_ranges = ranges;

                bool if_reachable = false;
                bool else_reachable = false;

                // Use the icmp instruction to update the if and else ranges
                handleICMP(icmp, if_ranges, else_ranges, if_reachable, else_reachable);

                bool changed = false;
                bool initialized = false;

                // Possible to get to this successor
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

                if (else_reachable) {
                     // Update else ranges
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
                // If branch is unconditional, always use the same range as before
                BasicBlock* parent = inst->getParent();
                BasicBlock* succ = dyn_cast<BasicBlock>(inst->getOperand(0));

                // Updated?
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

        // Get element pointer instructions
        void handleGEPOperations(Instruction* inst, Ranges& ranges) {
            // Nothing is assumed about values in arrays
            ranges[inst] = VariableRange();
        }

        // Call instructions
        void handleCallOperations(Instruction* inst, Ranges& ranges) {
            // Nothing is assumed about calls
            ranges[inst] = VariableRange();
        }

        // Cast instructions simply get the same range as the value we are casting from.
        void handleCastOperations(Instruction* inst, Ranges& ranges) {
            assert(ranges.count(inst->getOperand(0)));
            ranges[inst] = ranges[inst->getOperand(0)];
        }

        /*
         * Description:
         * Main function that determines how variables are updated depending on the instruction.
         * If a range was updated, this returns true.
         */
        bool handleInst(Instruction* inst, Ranges& ranges) {
            // Update depending on the type of the instruction.
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

            // Check if range has been updated and update accordingly, widen if necessary
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

        /* 
         * Description:
         * If range is trending towards INT_MAX or INT_MIN, simply expand the range to INT_MAX or INT_MIN
         */
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

        /*
         * Description:
         * Get array sizes from all arrays in the function F
         */
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

        /*
         * Description:
         * Get the ranges that precedes the instruction listed.
         */
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

        /*
         * Description:
         * On error, prints the debug information a getElementPtr instruction may have incurred.
         */
        void printDebugInformation(Instruction* inst) {
            DILocation* loc = inst->getDebugLoc();
            if (!loc) {
                errs() << "WARNING: Array out of bounds access at ";
                errs() << *inst << "\n";
                errs() << "Please compile with -g to see line numbers.\n";
            }
            else {
                loc->getDirectory();
                errs() << loc->getFilename() << ":" << loc->getLine() << ":"; 
                errs() << loc->getColumn() << ": warning: Array out of bounds access.\n";
            }   
        }

        /*
         * Description:
         * Checks all of the array bounds in the function F. Determines if they will be indexed out of bounds.
         */
        void checkArrayBounds(Function& F) {
            // Iterate through all instructions
            for (BasicBlock& BB : F) {
                for (Instruction& I : BB) {
                    // An array access
                    if (isa<GetElementPtrInst>(&I)) {
                        if (!inst_to_ranges.count(&I)) {
                            // We determined this block was not reachable
                            continue;
                        }

                        Ranges& ranges = getBeforeRanges(&I);
                        AllocaInst* array = dyn_cast<AllocaInst>(I.getOperand(0));
                        assert(array_sizes.count(array));
                        int array_size = array_sizes[array];

                        // Get the range of the corresponding index
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

                        // If range is out of range of array size, print debug
                        if (outOfRange(range, array_size)) {
                            printDebugInformation(&I);
                        }
                    }
                }
            }
        }

        /*
         * Description:
         * Main code of the algorithm. This is what is called on each function.
         */
        virtual bool runOnFunction(Function &F) {
            // Reset member variables. Necessary since this carries over between functions
            bb_succs.clear();
            inst_to_ranges.clear();
            basic_block_before_ranges.clear();
            bb_to_succ_ranges.clear();
            array_sizes.clear();

            // Find successsors for each block
            createSuccessorMap(F);

            // Continually iterate through this until convergence.
            bool changed = true;
            int count = 0;
            while (changed) {
                changed = false;

                // Iterate in bfs order
                BasicBlock& init = F.getEntryBlock();
                queue<BasicBlock*> bfs;
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

                        // Go through all predecessors to merge
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

                    // No predecessors reached this block and this is not the entry block.
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

                    // Update the variables in the function. If there are any changes, denote them
                    for (Instruction& I : *current) {
                        changed = handleInst(&I, unioned) || changed;
                    }
                            
                }

            }

            // Get all of the arrays
            getArrayInformation(F);

            // Check to see if range is out of bounds
            checkArrayBounds(F);

            // Since nothing was changed in the function, return false
            return false;
        }
    private:
        // successors of each basic block
        unordered_map<BasicBlock*, vector<BasicBlock*>> bb_succs;

        // Maps each instruction to all of the ranges known at that point in the program
        unordered_map<Instruction*, Ranges> inst_to_ranges;

        // On entry, the set of ranges of each basic block
        unordered_map<BasicBlock*, Ranges> basic_block_before_ranges;

        // For each successor to a basic block, denote what range corresponds to that block
        unordered_map<BasicBlock*, unordered_map<BasicBlock*, Ranges> > bb_to_succ_ranges;

        // Stores the array sizes of all arrays in the function
        unordered_map<AllocaInst*, int> array_sizes;
    };
}

// Necessary LLVM information

char BoundsCheckPass::ID = 0;

static RegisterPass<BoundsCheckPass> X("BoundsCheck", "Bounds Check Pass");

static void registerBoundsCheckPass(const PassManagerBuilder &,
                            legacy::PassManagerBase &PM) {
    PM.add(new BranchProbabilityInfoWrapperPass());
    PM.add(new BlockFrequencyInfoWrapperPass());                        
    PM.add(new BoundsCheckPass());
}
static RegisterStandardPasses
  RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                 registerBoundsCheckPass);
