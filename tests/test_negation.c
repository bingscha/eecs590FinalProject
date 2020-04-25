int temp() {
    return 10;
}

int main() {
    int i = 0;
    int j = 1000000;
    int k = 10;
    int interestingName[1000];
    j = -j + k;

    while (i < 10000) {
        if (i > 989) {
            interestingName[i + j] = k;
        }
        ++i;
    }
}