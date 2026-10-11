#pragma once

#include "executor.hpp"

#include <coroutine>

namespace asyncrt {

// The one concrete MVP awaitable that actually suspends and re-enters the
// ready queue rather than resolving synchronously via symmetric transfer --
// this is what exercises the executor's scheduling logic (see
// executor_tests.cpp's interleaving test), not just call-chain resolution.
struct YieldNow {
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> h) const { Executor::instance().schedule(h); }
  void await_resume() const noexcept {}
};

inline YieldNow yield_now() noexcept { return {}; }

} // namespace asyncrt
