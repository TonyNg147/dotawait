# async_project

A build-time tool that lowers Rust-flavored async syntax into real C++20
coroutine code, so you never hand-write `promise_type`/awaiter/
`coroutine_handle` boilerplate yourself.

```cpp
async int compute(int x) {
    int y = helper(x).await;
    return y * 2;
}
```

is transpiled into:

```cpp
#include <asyncrt/asyncrt.hpp>
::asyncrt::Task<int> compute(int x) {
    int y = co_await (helper(x));
    co_return y * 2;
}
```

## Layout

- `transpiler/` — the `asynctp` CLI: a hand-rolled, zero-dependency lexer +
  parser + lowering pass + emitter that turns `.async.cpp` files into plain
  `.cpp`. No dependency on `runtime/`.
- `runtime/` — `asyncrt`: a header-only, zero-dependency `Task<T>` +
  single-threaded cooperative `Executor` the generated code plugs into. No
  dependency on `transpiler/` — usable directly by hand-written C++20 too.
- `examples/` — `hello.async.cpp` (basic `.await` chaining) and
  `yielding.async.cpp` (two tasks interleaved via `yield_now`), both built
  through the real generate-then-compile pipeline.
- `examples/consumer/` — a standalone CMake project that uses this repo the
  way an outside codebase would: `add_subdirectory()` it, then build an
  async library and an app with `add_async_library`/`add_async_executable`.
  Build it on its own (`cmake -S examples/consumer -B build-consumer`) or let
  `ctest` build and run it as the `consumer_example` test.

## Using it in your project

An `.async.cpp` file needs no runtime includes or `using` declarations:
asynctp injects `#include <asyncrt/asyncrt.hpp>` and emits fully-qualified
`::asyncrt::Task<T>`. Pass `--no-runtime-include` to opt out.

```cpp
async int fetch(int x) {
    return x * 2;
}

async int go() {
    int a = fetch(1).await;     // await a call directly
    auto task = fetch(20);      // or create the task first...
    int val = task.await;       // ...and await it later
    return a + val;
}

int main() {
    // main() isn't async, so it can't use .await -- block_on drives the
    // top-level task to completion instead.
    return asyncrt::block_on(go()) == 42 ? 0 : 1;
}
```

`.await` is all you need to chain async functions. Reach for `spawn`/`run`
only when you want several tasks running concurrently, and `yield_now()`
only when a task should explicitly let others take a turn:

```cpp
async void worker(std::string name) {
    std::cout << name << " start\n";
    asyncrt::yield_now().await;   // let the other worker run
    std::cout << name << " end\n";
}

int main() {
    asyncrt::spawn(worker("a"));
    asyncrt::spawn(worker("b"));
    asyncrt::run();               // prints: a start, b start, a end, b end
}
```

Runtime API (`asyncrt/asyncrt.hpp`):

| | |
|---|---|
| `block_on(task)` | Run `task` (and anything it spawns) to completion and return its result |
| `spawn(task)` | Hand a task to the executor to run concurrently; result discarded |
| `run()` | Run all spawned work until idle; rethrows the first exception a spawned task ended with |
| `yield_now()` | Await to let other ready tasks run |

### CMake

```cmake
add_subdirectory(async_project)   # or FetchContent; tests/examples stay off

add_async_executable(app main.async.cpp util.cpp)       # mix async + plain sources
add_async_library(net STATIC net/fetch.async.cpp)       # consumers get asyncrt too

add_executable(tool tool.cpp)                           # or on an existing target
target_async_sources(tool PRIVATE worker.async.cpp)
```

These transpile every `.async.cpp`, link `asyncrt`, keep `#include "..."`
working relative to the original source, and emit `#line` directives so
compiler errors point at your `.async.cpp`. Header-only use of the runtime
without the transpiler is just `target_link_libraries(x PRIVATE asyncrt::asyncrt)`.

## Building this repo

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

or with the plain Makefile (adding an example = drop `examples/<name>.async.cpp`
and add `<name>` to `EXAMPLES`):

```sh
make test     # builds asynctp, runtime, every test suite, and both examples, then runs them all
make clean
```

Golden expected outputs (`transpiler/tests/golden/*.expected.cpp`) must also
compile against `runtime/include` (the `golden_compile` test and the matching
`make test` step), so a fixture must declare whatever it awaits, normally as
an `async` function in the fixture itself.

## Working on this repo with Claude Code

`CLAUDE.md` holds the project rules. `.claude/agents/` defines three roles:
`architect` (BA + solution architect), `developer`, and `tester`.
`.claude/skills/` holds the playbooks and scripts they use (`verify`,
`ticket`, `golden-test`, `syntax-feature`, `ship`). The backlog is in
[`docs/tickets/`](docs/tickets/README.md). `/ship T-0001` (or `/ship next`)
takes one ticket through design, implementation, and independent testing.

## MVP scope

Only the `async` function-signature marker and `.await` are supported,
lowering to a `Task<T>`-returning coroutine on a single-threaded executor.
Deliberately out of scope for now: `join!`/`select!` combinators, async
closures/blocks, a `?`-operator, and real OS-backed async I/O (the
executor's ready-queue is already shaped so these can slot in later without
a rearchitecture).

## Known MVP limitations

- A variable or function literally named `async` is misdetected as the
  marker keyword.
- The function-name heuristic (first identifier followed by a balanced
  `(...)` immediately followed by `{`) breaks on a return type with its own
  unbalanced parens, e.g. a raw function-pointer return type — this is
  detected and reported as a parse error rather than silently mis-lowered.
- No `noexcept` or trailing return type (`-> T`) on `async`-marked function
  signatures; the grammar requires `{` immediately after the parameter list.
- `.await`'s backward expression scan doesn't skip over string literals
  (comments are handled); parenthesize a complex expression before `.await`
  if needed (e.g. `(a ? b : c).await`).
- Lambda-start detection after `[` only treats `return`, `throw`, and
  `co_return` as still being "expression position"; any other keyword
  immediately before a lambda literal is misdetected as a subscript.
- Async declarations in headers (no body) aren't supported — only
  `.async.cpp` translation units with full function bodies.
- GCC only realizes the C++20 standard's O(1)-stack guarantee for symmetric
  transfer starting at `-O1`; at `-O0` very deep `.await` chains can still
  overflow the stack (see `runtime/tests/task_tests.cpp`).
