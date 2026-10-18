# dotawait (async_project)

Rust-style `async fn` / `.await` for C++20. A build-time transpiler (`asynctp`)
lowers `.async.cpp` into real coroutine code; a tiny header-only runtime
(`asyncrt`) supplies `Task<T>` and a single-threaded cooperative executor.
The product is **developer ergonomics**: every change should make async C++
shorter to write, harder to misuse, or clearer when it fails.

## Architecture invariants (never break these)

- `transpiler/` and `runtime/` are independent. Neither includes, links, or
  references the other. Only `examples/` and `cmake/AsyncTranspile.cmake`
  join them. This is why each has its own copy of `tests/test_support.hpp`.
- Production code in `transpiler/` and `runtime/` is **zero-dependency**:
  standard library only. No Boost, no libclang, no parser generators.
- `runtime/` stays header-only (`asyncrt` is an `INTERFACE` target).
- The transpiler is a text-island rewriter, not a C++ parser: everything
  outside an `async` function's span passes through byte-for-byte. Lowered
  code uses fully qualified `::asyncrt::` names so user files need no
  `using`.
- Parse failures are reported as `ParseError` (with line/col), never as
  silently wrong output. A heuristic that can misfire must fail loudly.
- `Task` is lazy (nothing runs until awaited or driven), and completion uses
  symmetric transfer. Don't regress either.
- The Makefile mirrors the CMake build. Any new source file or test suite
  goes into **both**.

## Build and verify

The verify script wraps every step. Use it instead of hand-rolled commands:

```sh
.claude/skills/verify/scripts/verify.sh quick   # cmake build + ctest, skips consumer (~seconds)
.claude/skills/verify/scripts/verify.sh full    # quick + consumer_example + Makefile parity
.claude/skills/verify/scripts/verify.sh asan    # ASan+UBSan build in build-asan/
.claude/skills/verify/scripts/verify.sh all     # full + asan; required before a ticket is done
```

Raw equivalents: `cmake -S . -B build && cmake --build build --parallel &&
ctest --test-dir build --output-on-failure`; `make BUILD=build-make test`
(always pass `BUILD=` so the Makefile does not write into CMake's `build/`).

Transpile one file by hand to inspect output:
`build/transpiler/asynctp in.async.cpp -o /dev/stdout --no-line-directives`.

Toolchain: GCC 15 / CMake 3.20+, C++20. The build uses `-Wall -Wextra` (via
the Makefile); new code must compile warning-free.

## Code conventions (match the existing code, not generic C++ style guides)

- `snake_case` for functions, variables, files; `PascalCase` for types;
  2-space indent; `#pragma once`; namespaces `asynctp` / `asyncrt`, helpers
  in an anonymous namespace or `detail`.
- Fallible transpiler stages return `std::variant<T, ParseError>`; no
  exceptions in the transpiler pipeline. The runtime propagates user
  exceptions through `Task` via `std::exception_ptr`.
- AST fields are `std::string_view`s into the original source buffer. Keep
  the source alive; don't copy text you only need to slice.
- No raw `new`/`delete` for ownership. `coroutine_handle` ownership lives in
  RAII types (`Task`), released explicitly (`release()`) only at a handoff.
- Comments explain *why* and document known limitations at the declaration
  (see `lowering.hpp`). Keep that density; don't narrate obvious code.

## Tests

- Hand-rolled harness: `CHECK(cond)` / `CHECK_EQ(a, b)` from
  `tests/test_support.hpp`, one `void test_xxx()` per case, called from
  `main()`, which returns `report("<suite>")`. No GoogleTest/doctest.
- Transpiler: unit tests in `lexer_tests`, `parser_tests`, `lowering_tests`;
  end-to-end CLI output in `tests/golden/<case>.async.cpp` +
  `<case>.expected.cpp`, registered by name in `golden_tests.cpp` `main()`.
- Runtime: `task_tests`, `executor_tests`.
- Integration: `examples/*.async.cpp` run as tests; `examples/consumer/` is
  the outside-project check of `add_async_executable`/`add_async_library`.
- Every behavior change ships with a test that fails without it. Every
  lifted limitation updates **README.md "Known MVP limitations"** and the
  matching comment in the header that documents it.

## Agentic workflow

Work is tracked as tickets in `docs/tickets/` (format and status flow in
`docs/tickets/README.md`; manage them with the `ticket` skill's script).

| Role | Subagent | Owns |
|---|---|---|
| Technical BA + Solution Architect | `architect` | Interactively prototypes architectural boundaries with the human using `effective-html`. Turns ideas into `ready` tickets with blueprints, acceptance criteria, and structural layout file references. Writes only under `docs/`. |
| Developer | `developer` | Reads the approved ticket and layout wireframe grounds. Implements one `ready` ticket, with tests, until `verify.sh full` passes. |
| Tester / QA | `tester` | Independently tries to break the change against the criteria and structural flow maps: adversarial cases, golden diffs, ASAn, consumer build. Gives a pass/fail verdict. |

### Prototyping & Pipeline Lifecycle
1. **Interactive Design Phase (Manual Interaction):** Before structural engineering tickets move to execution, the human manually works with `@architect`. The architect maps component scopes, syntax states, or control flows using the `effective-html` layout prototyping pattern and outputs these visual blueprints to `docs/sketches/review-latest.html` using the `write` tool. 
2. **Worker Isolation Constraint:** The background agents (`developer` and `tester`) must stay idle and must not execute automated tasks against this project segment until the human explicitly evaluates the layout file grounds and moves the ticket to `ready`.
3. **Execution Delivery:** The `@developer` and `@tester` interpret both the text ticket fields and the architectural grounds inside `docs/sketches/` to execute their respective code updates and validation assertions.

`/ship T-NNNN` runs the full loop (architect -> developer -> tester -> fix
loop). Roles are separate subagents so that the tester judges the code
without the developer's reasoning in its context.

Ticket status changes are done with
`python3 .claude/skills/ticket/scripts/ticket.py`, never by hand-editing the
index table.

## Git

- Commit messages: imperative subject, optionally prefixed with the ticket
  (`T-0004: support noexcept on async functions`).
- One ticket per commit/branch where practical. Don't commit `build*/` or
  `scratchpad.md` (gitignored local notes).

