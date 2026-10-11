#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace asyncrt {

template <typename T = void>
class Task;

namespace detail {

// Shared promise machinery independent of the result type T.
struct TaskPromiseBase {
  std::coroutine_handle<> continuation{};
  std::exception_ptr exception{};

  // Symmetric transfer on completion: instead of resuming the continuation
  // directly (which would grow the call stack), this is returned from
  // await_suspend so the compiler-generated resumption machinery tail-
  // resumes it with no added stack frame. If nothing is awaiting this task
  // (e.g. it's the top-level task driven by block_on), there is no
  // continuation to resume, so control returns to the caller via
  // std::noop_coroutine() instead.
  struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }
    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
      auto cont = h.promise().continuation;
      return cont ? cont : std::noop_coroutine();
    }
    void await_resume() const noexcept {}
  };

  // Lazy: a Task does nothing until it is awaited or driven by block_on,
  // matching Rust's "futures do nothing until polled" semantics.
  std::suspend_always initial_suspend() const noexcept { return {}; }
  FinalAwaiter final_suspend() noexcept { return {}; }
  void unhandled_exception() noexcept { exception = std::current_exception(); }
};

template <typename T>
struct TaskPromise : TaskPromiseBase {
  std::optional<T> value{};

  Task<T> get_return_object() noexcept;

  template <typename U>
  void return_value(U&& v) {
    value.emplace(std::forward<U>(v));
  }

  // Rethrows a stored exception if the coroutine body threw; otherwise
  // moves the result out of storage. Called from Task<T>::await_resume() or
  // from block_on() for a top-level task.
  T result() {
    if (exception) std::rethrow_exception(exception);
    return std::move(*value);
  }
};

template <>
struct TaskPromise<void> : TaskPromiseBase {
  Task<void> get_return_object() noexcept;
  void return_void() noexcept {}

  void result() {
    if (exception) std::rethrow_exception(exception);
  }
};

} // namespace detail

// The user-visible coroutine return type a lowered `async` function produces.
// A Task is itself Awaitable (its own await_ready/await_suspend/
// await_resume), which is exactly what makes `inner_fn().await` -- lowered
// to `co_await (inner_fn())` -- valid inside another async function: the
// outer coroutine's co_await protocol talks directly to the Task<T> the
// inner call returned, chaining the outer coroutine as the inner task's
// continuation via symmetric transfer, with no extra wrapper type needed.
template <typename T>
class [[nodiscard]] Task {
public:
  using promise_type = detail::TaskPromise<T>;

  explicit Task(std::coroutine_handle<promise_type> h) noexcept : handle_(h) {}
  Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
  Task& operator=(Task&& other) noexcept {
    if (this != &other) {
      if (handle_) handle_.destroy();
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() {
    if (handle_) handle_.destroy();
  }

  bool await_ready() const noexcept { return false; } // lazy: never ready before it has even started
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
    handle_.promise().continuation = awaiting;
    return handle_; // symmetric transfer: directly resume the inner task
  }
  T await_resume() { return handle_.promise().result(); }

  // Entry point used by Executor::block_on to drive a top-level task.
  std::coroutine_handle<promise_type> handle() const noexcept { return handle_; }

  // Gives up ownership of the coroutine frame; the caller becomes
  // responsible for destroying it. Used by Executor::spawn.
  std::coroutine_handle<promise_type> release() noexcept { return std::exchange(handle_, {}); }

private:
  std::coroutine_handle<promise_type> handle_;
};

namespace detail {

template <typename T>
Task<T> TaskPromise<T>::get_return_object() noexcept {
  return Task<T>(std::coroutine_handle<TaskPromise<T>>::from_promise(*this));
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept {
  return Task<void>(std::coroutine_handle<TaskPromise<void>>::from_promise(*this));
}

} // namespace detail

} // namespace asyncrt
