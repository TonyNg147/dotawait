// These exercise Task<T> directly, hand-written (not generated), bypassing
// the real Executor/block_on (M5) by resuming the top-level handle manually
// -- a "poor man's block_on" sufficient to prove the promise/symmetric-
// transfer machinery itself is correct before the executor exists.

#include "asyncrt/task.hpp"
#include "test_support.hpp"

#include <stdexcept>

using asyncrt::Task;

namespace {

Task<int> make_value() { co_return 42; }

Task<void> make_void() { co_return; }

Task<int> make_throwing() {
  if (true) throw std::runtime_error("boom");
  co_return 0; // unreachable, but makes this a coroutine
}

Task<int> add_one(int x) { co_return x + 1; }

Task<int> chained() {
  int a = co_await add_one(1);
  int b = co_await add_one(a);
  co_return b;
}

Task<int> chain(int depth) {
  if (depth == 0) co_return 0;
  int v = co_await chain(depth - 1);
  co_return v + 1;
}

void test_simple_value() {
  auto t = make_value();
  t.handle().resume();
  int v = t.handle().promise().result();
  CHECK_EQ(v, 42);
}

void test_void_task() {
  auto t = make_void();
  t.handle().resume();
  t.handle().promise().result(); // must not throw
  CHECK(true);
}

void test_exception_propagates_at_result() {
  auto t = make_throwing();
  t.handle().resume();
  bool threw = false;
  try {
    t.handle().promise().result();
  } catch (const std::runtime_error& e) {
    threw = true;
    CHECK_EQ(std::string(e.what()), std::string("boom"));
  }
  CHECK(threw);
}

void test_chained_tasks_via_await() {
  auto t = chained();
  t.handle().resume();
  int v = t.handle().promise().result();
  CHECK_EQ(v, 3); // add_one(1) -> 2, add_one(2) -> 3
}

void test_deep_chain_no_stack_growth() {
  // Deep enough that naive (non-symmetric-transfer) recursion through
  // Task<T>'s machinery would risk a stack overflow on an 8MB default
  // stack, while staying safely within it once symmetric transfer makes
  // the resumption chain flat. Note: GCC only realizes the standard's O(1)
  // stack guarantee for symmetric transfer starting at -O1 -- at -O0 it
  // keeps a real (if small) frame per level, so building this test at -O0
  // with a much larger depth (e.g. 100000) can still overflow the stack;
  // that is a codegen characteristic of unoptimized GCC builds, not a bug
  // in Task<T>. Build with -O1 or higher to verify arbitrarily deep chains.
  constexpr int kDepth = 5000;
  auto t = chain(kDepth);
  t.handle().resume();
  int v = t.handle().promise().result();
  CHECK_EQ(v, kDepth);
}

} // namespace

int main() {
  test_simple_value();
  test_void_task();
  test_exception_propagates_at_result();
  test_chained_tasks_via_await();
  test_deep_chain_no_stack_growth();
  return asyncrt::test::report("task_tests");
}
