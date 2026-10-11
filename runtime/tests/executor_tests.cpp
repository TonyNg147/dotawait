#include "asyncrt/asyncrt.hpp"
#include "test_support.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using asyncrt::block_on;
using asyncrt::Task;
using asyncrt::yield_now;

namespace {

Task<int> compute_value() { co_return 7; }

Task<int> compute_with_yields() {
  co_await yield_now();
  co_await yield_now();
  co_return 99;
}

Task<void> worker(std::vector<std::string>& log, std::string name) {
  log.push_back(name + "0");
  co_await yield_now();
  log.push_back(name + "1");
  co_await yield_now();
  log.push_back(name + "2");
}

void test_block_on_basic() {
  int v = block_on(compute_value());
  CHECK_EQ(v, 7);
}

void test_block_on_drains_yields_within_same_task() {
  int v = block_on(compute_with_yields());
  CHECK_EQ(v, 99);
}

void test_yield_interleaving_proves_ready_queue_not_just_call_chaining() {
  std::vector<std::string> log;
  auto a = worker(log, "a");
  auto b = worker(log, "b");

  // Start both: each runs to its first yield_now, which schedules its
  // continuation onto the executor instead of resuming synchronously.
  a.handle().resume();
  b.handle().resume();

  asyncrt::Executor::instance().drain();

  std::vector<std::string> expected = {"a0", "b0", "a1", "b1", "a2", "b2"};
  CHECK(log == expected);
}

Task<int> worker_with_result(std::vector<std::string>& log) {
  log.push_back("r0");
  co_await yield_now();
  log.push_back("r1");
  co_return 5;
}

Task<void> failing_worker() {
  co_await yield_now();
  throw std::runtime_error("boom");
}

Task<int> parent_spawning_children(std::vector<std::string>& log) {
  asyncrt::spawn(worker(log, "c"));
  co_await yield_now();
  log.push_back("p");
  co_return 1;
}

void test_spawn_and_run_interleave() {
  std::vector<std::string> log;
  asyncrt::spawn(worker(log, "a"));
  asyncrt::spawn(worker(log, "b"));
  asyncrt::run();

  std::vector<std::string> expected = {"a0", "b0", "a1", "b1", "a2", "b2"};
  CHECK(log == expected);
}

void test_spawn_accepts_non_void_task() {
  std::vector<std::string> log;
  asyncrt::spawn(worker_with_result(log));
  asyncrt::run();

  std::vector<std::string> expected = {"r0", "r1"};
  CHECK(log == expected);
}

void test_run_rethrows_spawned_exception() {
  asyncrt::spawn(failing_worker());
  bool threw = false;
  try {
    asyncrt::run();
  } catch (const std::runtime_error& e) {
    threw = std::string(e.what()) == "boom";
  }
  CHECK(threw);

  // The failed task was reaped; a later run() has nothing left to rethrow.
  bool threw_again = false;
  try {
    asyncrt::run();
  } catch (...) {
    threw_again = true;
  }
  CHECK(!threw_again);
}

void test_block_on_also_runs_tasks_spawned_inside_it() {
  std::vector<std::string> log;
  int v = block_on(parent_spawning_children(log));
  CHECK_EQ(v, 1);

  std::vector<std::string> expected = {"c0", "p", "c1", "c2"};
  CHECK(log == expected);
}

} // namespace

int main() {
  test_block_on_basic();
  test_block_on_drains_yields_within_same_task();
  test_yield_interleaving_proves_ready_queue_not_just_call_chaining();
  test_spawn_and_run_interleave();
  test_spawn_accepts_non_void_task();
  test_run_rethrows_spawned_exception();
  test_block_on_also_runs_tasks_spawned_inside_it();
  return asyncrt::test::report("executor_tests");
}
