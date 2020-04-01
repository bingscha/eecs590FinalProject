#!/bin/bash
rm -vf *.bc

PATH_MYPASS=/home/bingscha/eecs590Project/build/value_range/LLVMJPT.so            ### Action Required: Specify the path to your pass ###
NAME_MYPASS=-ValueRange                                                           ### Action Required: Specify the name for your pass ###

# Convert source code to bitcode (IR)
# This approach has an issue with -O2, so we are going to stick with default optimization level (-O0)
clang -emit-llvm -c test_default.c -o test_default.bc

# Apply your pass to bitcode (IR)
opt -load ${PATH_MYPASS} ${NAME_MYPASS} -disable-output < test_default.bc