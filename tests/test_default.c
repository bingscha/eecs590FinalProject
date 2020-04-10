int main() {
    int i = 0;
    int j = 100000;
    int k = 10;
    int interestingName[1000];
    // interestingName[i];

    if (i < 10) {
        ++i;
    }
    // else if (i > 10) {
    //     ++i;
    // }
    // else if (i <= 10) {
    //     ++i;
    // }
    // else if (i >= 10) {
    //     ++i;
    // }
    // else if (i == 10) {
    //     ++i;
    // }
    // else if (i != 10) {
    //     ++i;
    // }
    // else if (!(i < 10) && i < j) {
    //     ++i;
    // }

    // TODO implement for infinite lattices

    while (i < 3) {
        k += 1;
        ++i;
    }
    return k;
}