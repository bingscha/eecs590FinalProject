// #ifndef RANGES_H
// #define RANGES_H

// #include "llvm/IR/Value.h"

// #include <unordered_map>

// using std::unordered_map;

// #include "VariableRange.h"

// struct Ranges {
//     size_t size() {
//         return ranges.size();
//     }

//     bool count(Value* val) {
//         return ranges.count(val);
//     }

//     bool erase(Value* val) {
//         ranges.erase(val);
//     }

//     unordered_map<Value*, VariableRange>::iterator begin() const {
//         return ranges.begin();
//     }

//     unordered_map<Value*, VariableRange>::iterator end() const {
//         return ranges.end();
//     }

//     bool operator==(Ranges& other) {
//         if (ranges.size() != other.size()) {
//             return false;
//         }

//         for (auto& range : ranges) {
//             Value* val = range.first;
//             VariableRange& varRange = range.second;
//             if (!other.count(val)) {
//                 return false;
//             }

//             if (other[val].min_value != varRange.min_value || other[val].max_value != varRange.max_value) {
//                 return false;
//             }
//         }

//         return true;
//     }

//     VariableRange& operator[](Value* val) {
//         return ranges[val];
//     }

//     unordered_map<Value*, VariableRange> ranges;

// };

// #endif