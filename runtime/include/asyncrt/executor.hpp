#pragma once

#include "task.hpp"

#include <coroutine>
#include <deque>
#include <exception>
#include <utility>
#include <vector>

namespace asyncrt {

// Single-threaded cooperative run loop: a ready-queue of coroutine_handle<>
// that generated/runtime code (currently just YieldNow, see yield.hpp)
// schedules continuations onto, drained one at a time until empty. No
// threads, no real OS I/O -- MVP scope deliberately stops here. Real async
// I/O would later register OS completion callbacks that call schedule(h)
// when ready, reusing this same queue without any rearchitecture.
class Executor {
public:
  static Executor& instance() {
    static Executor exec;
    return exec;
  }

  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;

  ~Executor() {
    for (auto& s : spawned_) s.handle.destroy();
  }

  void schedule(std::coroutine_handle<> h) { ready_queue_.push_back(h); }

  // Takes ownership of `t` and queues it to start on the next drain(), so
  // several spawned tasks interleave through the ready queue instead of
  // running one after another. The task's result (if any) is discarded; an
  // exception it ends with is rethrown by reap().
  template <typename T>
  void spawn(Task<T> t) {
    auto h = t.release();
    spawned_.push_back({h, &h.promise()});
    schedule(h);
  }

  void drain() {
    while (!ready_queue_.empty()) {
      auto h = ready_queue_.front();
      ready_queue_.pop_front();
      h.resume();
    }
  }

  // Destroys every spawned task that has finished, then rethrows the first
  // exception any of them ended with. Tasks still suspended (waiting on
  // something that hasn't scheduled them yet) are kept for a later drain.
  void reap() {
    std::exception_ptr first;
    std::erase_if(spawned_, [&](const Spawned& s) {
      if (!s.handle.done()) return false;
      if (!first && s.promise->exception) first = s.promise->exception;
      s.handle.destroy();
      return true;
    });
    if (first) std::rethrow_exception(first);
  }

private:
  struct Spawned {
    std::coroutine_handle<> handle;
    detail::TaskPromiseBase* promise;
  };

  Executor() = default;
  std::deque<std::coroutine_handle<>> ready_queue_;
  std::vector<Spawned> spawned_;
};

// Fire-and-forget: hands `t` to the executor to run concurrently with
// everything else on the next run()/block_on().
template <typename T>
void spawn(Task<T> t) {
  Executor::instance().spawn(std::move(t));
}

// Runs every spawned/scheduled task until there is no more ready work, then
// rethrows the first exception a spawned task ended with (if any).
inline void run() {
  auto& exec = Executor::instance();
  exec.drain();
  exec.reap();
}

// Top-level entry point: resumes `t`'s handle directly (it has no awaiting
// coroutine above it -- this is the synchronous/non-coroutine boundary from
// main()), then runs the executor to flush any work scheduled via
// yield_now(), spawn(), or future scheduling primitives, then returns the
// result (or rethrows a propagated exception).
template <typename T>
T block_on(Task<T> t) {
  auto h = t.handle();
  h.resume();
  run();
  return h.promise().result();
}

} // namespace asyncrt
