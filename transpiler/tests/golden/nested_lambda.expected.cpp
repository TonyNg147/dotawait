#include <asyncrt/asyncrt.hpp>
::asyncrt::Task<int> fetch(int v) { co_return v * 10; }

::asyncrt::Task<int> run_all(int x) {
    auto add_one = [](int a) {
        return a + 1;
    };
    int v = add_one(x);
    if (v > 0) {
        co_return co_await (fetch(v));
    }
    co_return 0;
}
