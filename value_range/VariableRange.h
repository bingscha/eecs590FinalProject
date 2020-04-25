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
#include <iostream>
#include <limits.h>

// usings
using namespace llvm;

using std::ostream;
using std::max;
using std::min;

// Struct that represents the variable range. By default it is the range from INT_MIN and INT_MAX
struct VariableRange {
    int min_value = INT_MIN;
    int max_value = INT_MAX;

    bool operator==(VariableRange& other) {
        return min_value == other.min_value && max_value == other.max_value;
    }
};

// Print out a range to os
ostream& operator<<(ostream& os, const VariableRange& range) {
    os << range.min_value << "\t" << range.max_value << "\n";
    return os;
}

// Determines if a range exceeds the bounds of an array
bool outOfRange(const VariableRange& range, int array_size) {
    if (range.max_value < 0) {
        return true;
    }

    if (range.min_value >= array_size) {
        return true;
    }

    return false;
}

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

// Check all combinations of the op on the left and right end of each range
VariableRange checkAllCombinations(const VariableRange& lhs, const VariableRange& rhs, char op) {
    int min_value = INT_MAX;
    int max_value= INT_MIN;

    // Check the smallest value possible
    min_value = min(min_value, checkUnderOverFlow(lhs.min_value, rhs.min_value, op));
    min_value = min(min_value, checkUnderOverFlow(lhs.min_value, rhs.max_value, op));
    min_value = min(min_value, checkUnderOverFlow(lhs.max_value, rhs.min_value, op));
    min_value = min(min_value, checkUnderOverFlow(lhs.max_value, rhs.max_value, op));

    // For division, if 1 or -1 is in the range it can yield the smallest value
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

    // For division, if 1 or -1 is in the range it can yield the largest value
    if (op == '/' && rhs.min_value < -1 && -1 < rhs.max_value) {
        max_value = max(max_value, checkUnderOverFlow(lhs.min_value, -1, op));
        max_value = max(max_value, checkUnderOverFlow(lhs.max_value, -1, op));
    } 
    else if (op == '/' && rhs.min_value < 1 && 1 < rhs.max_value) {
        max_value = max(max_value, checkUnderOverFlow(lhs.min_value, 1, op));
        max_value = max(max_value, checkUnderOverFlow(lhs.max_value, 1, op));
    }

    return { min_value, max_value };
}

// If adding two variables, returns the new range when adding together.
VariableRange addRanges(const VariableRange& lhs, const VariableRange& rhs) {
    return checkAllCombinations(lhs, rhs, '+');
}

// If adding two variables, returns the new range when adding together.
VariableRange subRanges(const VariableRange& lhs, const VariableRange& rhs) {
    return checkAllCombinations(lhs, rhs, '-');
}

// Multiplying two ranges, returns the new range when multiplying together.
VariableRange multRanges(const VariableRange& lhs, const VariableRange& rhs) {
    return checkAllCombinations(lhs, rhs, '*');
}

// Dividing two ranges, returns the new range when dividing together.
VariableRange divRanges(const VariableRange& lhs, const VariableRange& rhs) {
    if (rhs.min_value == 0 && rhs.max_value == 0) {
        errs() << "ERROR: Divide by 0 attempted, exitting.\n";
        exit(1);
    }

    return checkAllCombinations(lhs, rhs, '/');
}

// Make sure the range makes sense, min is less than max
bool validate(const VariableRange& range) {
    return range.max_value >= range.min_value;
}

// Returns the range if lhs < rhs for lhs
// If this range is not possible, for example [3, 4] < [1, 3], the successful is false
VariableRange lessRange(const VariableRange& lhs, const VariableRange& rhs, bool& successful) {
    VariableRange output = lhs;
    output.max_value = min(rhs.max_value - 1, lhs.max_value);

    if (validate(output)) {
        successful = true;
        return output;
    }
    else {
        successful = false;
        return {INT_MAX, INT_MAX};
    }
}

// Returns the range if lhs <= rhs for lhs
VariableRange lessEqualRange(const VariableRange& lhs, const VariableRange& rhs, bool& successful) {
    VariableRange output = lhs;
    output.max_value = min(rhs.max_value, lhs.max_value);

    if (validate(output)) {
        successful = true;
        return output;
    }
    else {
        successful = false;
        return {INT_MAX, INT_MAX};
    }
}

// Returns the range if lhs > rhs for lhs
VariableRange greaterRange(const VariableRange& lhs, const VariableRange& rhs, bool& successful) {
    VariableRange output = lhs;
    output.min_value = min(rhs.min_value + 1, lhs.max_value);

    if (validate(output)) {
        successful = true;
        return output;
    }
    else {
        successful = false;
        return {INT_MAX, INT_MAX};
    }
}

// Returns the range if lhs >= rhs for lhs
VariableRange greaterEqualRange(const VariableRange& lhs, const VariableRange& rhs, bool& successful) {
    VariableRange output = lhs;
    output.min_value = min(rhs.min_value, lhs.max_value);

    if (validate(output)) {
        successful = true;
        return output;
    }
    else {
        successful = false;
        return {INT_MAX, INT_MAX};
    }
}

// Returns the range if lhs == rhs for lhs
VariableRange equalRange(const VariableRange& lhs, const VariableRange& rhs, bool& successful) {
    VariableRange output = lessEqualRange(lhs, rhs, successful);
    if (successful) {
        output = greaterEqualRange(output, rhs, successful);
    }

    return output;
}

#endif