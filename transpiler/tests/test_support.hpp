#pragma once

// Minimal, zero-dependency test harness used until CMake/FetchContent brings
// in doctest for the real test suites (M6). Each CHECK records a failure but
// keeps running so one run reports every mismatch, not just the first.

#include <iostream>
#include <string>

namespace asynctp::test {

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

} // namespace asynctp::test

#define CHECK(cond) ::asynctp::test::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::asynctp::test::check((a) == (b), #a " == " #b, __FILE__, __LINE__)
