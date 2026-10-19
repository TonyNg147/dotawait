#include <asyncrt/asyncrt.hpp>
#include <utility>
::asyncrt::Task<int> g() { co_return 1; }
::asyncrt::Task<std::pair<int, int>> f(int a = (1), char b = ')') {
  // it's a comment with } brace
  auto s = R"x(})x";
  int v = co_await (g());
  auto l = [] { return 2; };
  co_return std::make_pair(v + a + b + int(s[0]), l());
}
int main() {}
