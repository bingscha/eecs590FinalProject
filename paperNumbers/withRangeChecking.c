#include <stdlib.h>
#include <vector>

using namespace std;

#define ITERATIONS 100000000

int main() {
    vector<int> to_access(ITERATIONS);

    for (int i = 0; i < ITERATIONS; ++i) {
        int idx = rand() % ITERATIONS;
        to_access.at(idx) = rand();
    }

    long long sum = 0;
    for (int i = 0; i < ITERATIONS; ++i) {
        int idx = rand() % ITERATIONS;
        sum += to_access.at(idx);
    }

    return sum;
}