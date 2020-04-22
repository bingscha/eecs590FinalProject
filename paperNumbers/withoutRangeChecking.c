#include <stdio.h> 
#include <stdlib.h> 

#define ITERATIONS 100000000

int main() {
    int* to_access = (int *)malloc(sizeof(int)*(ITERATIONS));

    for (int i = 0; i < ITERATIONS * 10; ++i) {
        int idx = rand() % ITERATIONS;
        to_access[idx] = rand();
    }

    long long sum = 0;
    for (int i = 0; i < ITERATIONS * 10; ++i) {
        int idx = rand() % ITERATIONS;
        sum += to_access[idx];
    }
    
    free(to_access);

    return sum;
}