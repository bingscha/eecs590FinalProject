#include <stdlib.h>

int main() {
    int array[30];
    for (int i = 0; i < 30; ++i) {
        array[i] = i;
    }

    int random_index = rand();

    // Indexing with random, do not know value of variable
    int random_var = array[random_index];

    if (random_var > 10) {
        int j = array[random_var];
        int sum = 0;
        int k = random_var + 15;
        for (; k < 40; ++k) {
            sum += array[k + 5]; // Should be an out of bounds, random_var > 10, k > 25, k + 5 > 30
        }

        if (sum < 0) {
            sum *= -1;
        }
        
        sum += 1;
        sum *= 50;
        while (random_var > sum) {
            k += array[random_var]; // Will always index out of bounds
        }
    }

    return array[0];
}