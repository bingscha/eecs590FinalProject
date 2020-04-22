#include <stdlib.h>

int main() {
    int array[30];
    int i = 0;
    while(i > -1) {
        array[i] = i;
        ++i;
    }

    return array[0];
}