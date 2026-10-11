#include <asyncrt/asyncrt.hpp>
int helper(int x) { return x + 1; }

::asyncrt::Task<int> compute(int x) {
    int y = co_await (helper(x));
    co_return y * 2;
}
