rm -f without with
g++ withoutRangeChecking.c -o without
g++ withRangeChecking.c -o with

time ./without
time ./with

rm -f without with