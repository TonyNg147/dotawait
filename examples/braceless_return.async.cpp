#include <iostream>

// Every early `return` below is braceless, exactly as in ordinary C++; the
// transpiler must lower each one to `co_return` for this file to compile.

async int square(int v) { return v * v; }

async int sign(int x) {
    if (x > 0) return 1;
    else if (x < 0) return -1;
    else return 0;
}

async int classify(int k) {
    switch (k) {
        case 0: return 100;
        case 1: return square(k + 6).await;
        default: return -1;
    }
}

async int first_multiple_of(int step, int at_least) {
    for (int v = step;; v += step)
        if (v >= at_least) return square(v).await / v;
}

async int countdown(int n) {
    while (n > 0) return n;
    return -1;
}

async int first_odd_square(int from) {
    do if (from % 2) return square(from).await; else ++from; while (true);
}

async int skip_to(int k) {
    if (k) goto done;
    return 0;
done:
    return k + square(2).await;
}

int main() {
    struct Case { int got; int want; };
    const Case cases[] = {
        {asyncrt::block_on(sign(5)), 1},
        {asyncrt::block_on(sign(-5)), -1},
        {asyncrt::block_on(sign(0)), 0},
        {asyncrt::block_on(classify(0)), 100},
        {asyncrt::block_on(classify(1)), 49},
        {asyncrt::block_on(classify(7)), -1},
        {asyncrt::block_on(first_multiple_of(4, 10)), 12},
        {asyncrt::block_on(countdown(3)), 3},
        {asyncrt::block_on(countdown(0)), -1},
        {asyncrt::block_on(first_odd_square(4)), 25},
        {asyncrt::block_on(skip_to(0)), 0},
        {asyncrt::block_on(skip_to(3)), 7},
    };

    int failures = 0;
    for (const auto& c : cases) {
        if (c.got != c.want) {
            std::cerr << "got " << c.got << ", want " << c.want << "\n";
            failures++;
        }
    }
    if (failures == 0) std::cout << "braceless_return: all cases OK" << std::endl;
    return failures == 0 ? 0 : 1;
}
