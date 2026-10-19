#include <utility>
async int g() { return 1; }
async std::pair<int, int> f(int a = (1), char b = ')') {
  // it's a comment with } brace
  auto s = R"x(})x";
  int v = g().await;
  auto l = [] { return 2; };
  return std::make_pair(v + a + b + int(s[0]), l());
}
int main() {}
