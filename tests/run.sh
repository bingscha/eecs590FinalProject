#!/bin/bash
rm -f *.bc

PATH_MYPASS=/home/bingscha/eecs590Project/build/value_range/LLVMJPT.so            ### Action Required: Specify the path to your pass ###
NAME_MYPASS=-BoundsCheck                                                           ### Action Required: Specify the name for your pass ###

for filename in test*.c; do
    /home/bingscha/bin/bin/clang -emit-llvm -c -g ${filename} -o test.bc

    /home/bingscha/bin/bin/opt -load ${PATH_MYPASS} ${NAME_MYPASS} -disable-output < test.bc
done

rm test.bc
# Apply your pass to bitcode (IR)
