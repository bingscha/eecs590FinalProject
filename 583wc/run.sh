#!/bin/bash
### benchmark runner script
### Locate this script at each benchmark directory. e.g, 583simple/run.sh
### usage: ./run.sh ${benchmark_name} ${input} 
### e.g., ./run.sh compress compress.in   or   ./run.sh simple

rm -v *prof*
rm -v cccp.c
rm -v wc*

PATH_MYPASS=${LLVM_HW1_PASS_DIR}                                                      ### Action Required: Specify the path to your pass ###
NAME_MYPASS=-StatisticsPass                                                           ### Action Required: Specify the name for your pass ###
BENCH=../src/wc.c
INPUT=cccp.c

setup(){
  if [[ ! -z "${INPUT}" ]]; then
    echo "INPUT:${INPUT}"
    ln -sf ../input1/${INPUT} .
  fi
}


# Prepare input to run
setup
# Convert source code to bitcode (IR)
# This approach has an issue with -O2, so we are going to stick with default optimization level (-O0)
clang -emit-llvm -c ${BENCH} -o wc.bc  
# Instrument profiler
opt -pgo-instr-gen -instrprof wc.bc -o wc.prof.bc
# Generate binary executable with profiler embedded
clang -fprofile-instr-generate wc.prof.bc -o wc.prof
# Collect profiling data
./wc.prof ${INPUT}
# Translate raw profiling data into LLVM data format
llvm-profdata merge -output=pgo.profdata code-*.profraw

# Prepare input to run
setup
# Apply your pass to bitcode (IR)
opt -pgo-instr-use -pgo-test-profile-file=pgo.profdata -load ${PATH_MYPASS} ${NAME_MYPASS} -disable-output < wc.bc 2> 583wc.opcstats