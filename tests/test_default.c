int temp() {
    return 10;
}

int main() {
    int i = 0;
    int j = 100000;
    int k = 10;
    int interestingName[1000];

    while (i < 10000) {
        if (i > 989) {
            interestingName[i + k] = j;
        }
        ++i;
    }

    interestingName[temp()] = temp();
    int d = interestingName[10];
    i = j;

    if (i > 1001) {
        interestingName[i] = j;
    }

    for (int i = 0; i < 10000; ++i) {
        int k = interestingName[i];
    }

    // k = k - j;
    // k = k / j;
    // k = k * j;

    // // TODO implement for infinite lattices

    // while (i < 3) {
    //     k += 1;
    //     ++i;
    // }
    // return k;
}