int temp() {
    return 10;
}

int main() {
    int i = 0;
    int j = 100000;
    int k = 10;
    int interestingName[1000];

    while (i < k) {
        if (i > 989) {
            interestingName[i + k] = j;
        }
        ++i;
        --k;
        interestingName[k] = -1;
    }
}