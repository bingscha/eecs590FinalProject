int main() {
    int i = 0;
    int j = 100000000;
    int k = 10;
    int interestingName[1000];
    // interestingName[i];

    if (i < 10) {
        ++i;
    }
    else if (i > 10) {
        ++i;
    }
    else if (i <= 10) {
        ++i;
    }
    else if (i >= 10) {
        ++i;
    }
    else if (i == 10) {
        ++i;
    }
    else if (i != 10) {
        ++i;
    }
    else if (!(i < 10) && i < j) {
        ++i;
    }


    while (i < 20) {
        k += j;
    }
    return k;
}