#ifndef VARIABLE_RANGE_H
#define VARIABLE_RANGE_H

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

// STL Includes
#include <algorithm>
#include <limits.h>

using namespace llvm;

using std::max;
using std::min;

struct VariableRange {
    int min_value = INT_MIN;
    int max_value = INT_MAX;
};

// Check if specific operation will cause overflow or underflow.
// If it does, return the corresponding value.
int checkUnderOverFlow(long long lhs, long long rhs, char operation) {
    switch(operation) {
        case '+':
            if (lhs + rhs > INT_MAX) {
                return INT_MAX;
            }
            else if (lhs + rhs < INT_MIN) {
                return INT_MIN;
            }
            return static_cast<int>(lhs + rhs);
        case '-':
            if (lhs - rhs > INT_MAX) {
                return INT_MAX;
            }
            else if (lhs - rhs < INT_MIN) {
                return INT_MIN;
            }
            return static_cast<int>(lhs - rhs);
        case '*':
            if (lhs * rhs > INT_MAX) {
                return INT_MAX;
            }
            else if (lhs * rhs < INT_MIN) {
                return INT_MIN;
            }
            return static_cast<int>(lhs * rhs);
        case '/':
            if (rhs == 0) {
                return lhs;
            }
            else if (lhs / rhs > INT_MAX) {
                return INT_MAX;
            }
            else if (lhs / rhs < INT_MIN) {
                return INT_MIN;
            }
            return static_cast<int>(lhs / rhs);
        default:
            errs() << "INVALID ARGUMENT IN CHECKOVERFLOW FUNCTION: Exitting\n";
            exit(1);
    }
}

// Unions two ranges such that 
VariableRange unionRange(const VariableRange& lhs, const VariableRange& rhs) {
    return VariableRange{min(lhs.min_value, rhs.min_value), max(lhs.max_value, rhs.max_value)};
}

VariableRange checkAllCombinations(const VariableRange& lhs, const VariableRange& rhs, char op) {
    int min_value = INT_MAX;
    int max_value= INT_MIN;

    // Check the smallest value possible
    min_value = min(min_value, checkUnderOverFlow(lhs.min_value, rhs.min_value, op));
    min_value = min(min_value, checkUnderOverFlow(lhs.min_value, rhs.max_value, op));
    min_value = min(min_value, checkUnderOverFlow(lhs.max_value, rhs.min_value, op));
    min_value = min(min_value, checkUnderOverFlow(lhs.max_value, rhs.max_value, op));

    if (op == '/' && rhs.min_value < -1 && -1 < rhs.max_value) {
        min_value = min(min_value, checkUnderOverFlow(lhs.min_value, -1, op));
        min_value = min(min_value, checkUnderOverFlow(lhs.max_value, -1, op));
    } 
    else if (op == '/' && rhs.min_value < 1 && 1 < rhs.max_value) {
        min_value = min(min_value, checkUnderOverFlow(lhs.min_value, 1, op));
        min_value = min(min_value, checkUnderOverFlow(lhs.max_value, 1, op));
    }

    // Check the largest value possible
    max_value = max(max_value, checkUnderOverFlow(lhs.min_value, rhs.min_value, op));
    max_value = max(max_value, checkUnderOverFlow(lhs.min_value, rhs.max_value, op));
    max_value = max(max_value, checkUnderOverFlow(lhs.max_value, rhs.min_value, op));
    max_value = max(max_value, checkUnderOverFlow(lhs.max_value, rhs.max_value, op));

    if (op == '/' && rhs.min_value < -1 && -1 < rhs.max_value) {
        max_value = max(max_value, checkUnderOverFlow(lhs.min_value, -1, op));
        max_value = max(max_value, checkUnderOverFlow(lhs.max_value, -1, op));
    } 
    else if (op == '/' && rhs.min_value < 1 && 1 < rhs.max_value) {
        max_value = max(max_value, checkUnderOverFlow(lhs.min_value, 1, op));
        max_value = max(max_value, checkUnderOverFlow(lhs.max_value, 1, op));
    }

    return {min_value, max_value};
}

// If adding two variables, returns the new range when adding together
VariableRange addRanges(const VariableRange& lhs, const VariableRange& rhs) {
    return checkAllCombinations(lhs, rhs, '+');
}

// If adding two variables, returns the new range when adding together
VariableRange subRanges(const VariableRange& lhs, const VariableRange& rhs) {
    return checkAllCombinations(lhs, rhs, '-');
}

VariableRange multRanges(const VariableRange& lhs, const VariableRange& rhs) {
    return checkAllCombinations(lhs, rhs, '*');
}

VariableRange divRanges(const VariableRange& lhs, const VariableRange& rhs) {
    if (rhs.min_value == 0 && rhs.max_value == 0) {
        errs() << "ERROR: Divide by 0 attempted, exitting.\n";
        exit(1);
    }

    return checkAllCombinations(lhs, rhs, '/');
}

#endif