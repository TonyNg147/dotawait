#include <asyncrt/asyncrt.hpp>
constexpr int kMax = 1'000;
::asyncrt::Task<int> f() { co_return kMax; }
