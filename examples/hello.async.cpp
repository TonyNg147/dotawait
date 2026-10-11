#include <iostream>

async int add_one(int x) {
    return x + 1;
}

async int compute(int x) {
    int y = add_one(x).await;
    auto task = add_one(y);   // a task can also be created now and awaited later
    int z = task.await;
    return z;
}

int main() {
    int result = asyncrt::block_on(compute(10));
    std::cout << "result = " << result << std::endl;
    return result == 12 ? 0 : 1;
}
