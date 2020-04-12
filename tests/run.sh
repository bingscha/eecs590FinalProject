#!/bin/bash
rm -vf *.bc

PATH_MYPASS=/home/bingscha/eecs590Project/build/value_range/LLVMJPT.so            ### Action Required: Specify the path to your pass ###
NAME_MYPASS=-ValueRange                                                           ### Action Required: Specify the name for your pass ###

/home/bingscha/bin/bin/clang -emit-llvm -c -g test_default.c -o test_default.bc

# Apply your pass to bitcode (IR)
/home/bingscha/bin/bin/opt -load ${PATH_MYPASS} ${NAME_MYPASS} -disable-output < test_default.bc