#include <asyncrt/asyncrt.hpp>
::asyncrt::Task<int> fetch(int v) { co_return v * 10; }

::asyncrt::Task<int> classify(int k) {
    switch (k) {
        case 0: co_return 100;
        case 1:
            co_return co_await (fetch(k));
        default: co_return -1;
    }
}

::asyncrt::Task<int> sign(int x) {
    if (x > 0) co_return 1;
    else if (x < 0) co_return -1;
    else co_return 0;
}
