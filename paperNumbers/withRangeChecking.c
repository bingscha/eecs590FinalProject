#include <stdio.h> 
#include <stdlib.h> 

#define ITERATIONS 100000000

int main() {
    int* to_access = (int *)malloc(sizeof(int)*ITERATIONS);

    for (int i = 0; i < ITERATIONS * 10; ++i) {
        int idx = rand() % ITERATIONS;
        if (idx >= 0 && idx < ITERATIONS) {
            to_access[idx] = rand();
        }
        else {
            exit(1);   
        }
    }

    long long sum = 0;
    for (int i = 0; i < ITERATIONS * 10; ++i) {
        int idx = rand() % ITERATIONS;
        if (idx >= 0 && idx < ITERATIONS) {
            sum += to_access[idx];
        }
        else {
            exit(1);   
        }
    }

    free(to_access);

    return sum;
}