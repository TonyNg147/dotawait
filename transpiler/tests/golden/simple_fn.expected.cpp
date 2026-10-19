#include <asyncrt/asyncrt.hpp>
int helper(int x) { return x + 1; }

::asyncrt::Task<int> helper_async(int x) { co_return helper(x); }

::asyncrt::Task<int> compute(int x) {
    int y = co_await (helper_async(x));
    co_return y * 2;
}
