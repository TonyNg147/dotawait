#pragma once

// Shared with transpiler/tests/test_support.hpp in spirit, but kept as its
// own copy so runtime/ has zero dependency on transpiler/ (see the module
// boundary described in the project plan).

#include <iostream>
#include <string>

namespace asyncrt::test {

inline int& failure_count() {
  static int count = 0;
  return count;
}

inline void check(bool cond, const std::string& expr, const char* file, int line) {
  if (!cond) {
    failure_count()++;
    std::cerr << file << ":" << line << ": CHECK FAILED: " << expr << "\n";
  }
}

inline int report(const char* suite_name) {
  if (failure_count() == 0) {
    std::cout << suite_name << ": all checks passed\n";
    return 0;
  }
  std::cerr << suite_name << ": " << failure_count() << " check(s) failed\n";
  return 1;
}

} // namespace asyncrt::test

#define CHECK(cond) ::asyncrt::test::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::asyncrt::test::check((a) == (b), #a " == " #b, __FILE__, __LINE__)
