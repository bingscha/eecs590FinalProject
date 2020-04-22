#include <stdlib.h>

int main() {
    int index = rand();
    int sign = 1;

    // In all cases, should result in a negative integer
    if (index > 0) {
        sign = -1;
        index *= sign * 30;
    }
    else if (index < 0) {
        sign = 1;
        index *= sign * 30;
    }
    else {
        index -= 1;
    }

    int array[1];
    array[index] = 10;
    return array[index];
}