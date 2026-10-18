---
id: T-0031
title: Add the tests-fail-on-base gate for agent PRs
status: ready
area: cross-cutting
type: feature
priority: P2
depends_on: [T-0028]
created: 2026-10-18
---

Epic: T-0025. Design doc: `docs/design/agent-pipeline-security.md`
threat T4; canary C15.

This ticket touches `.github/`, so it is a human or local `/ship`
ticket.

## User story

As the human merging agent PRs, I want proof that the PR's tests would
have failed without its production change. This is CLAUDE.md's "Every
behavior change ships with a test that fails without it", checked by a
machine instead of taken on trust.

## Problem

CLAUDE.md requires a test that fails without the change, and the
developer agent is told to watch tests fail first (`developer.md` step
3). Nothing verifies it: a PR whose new tests pass on the old code is
green today.

I ran the mechanism by hand on 2026-10-18 against a simulated agent PR,
branch `agent/T-0001` in a scratch clone. The PR adds:
- `test_braceless_if_return` to `transpiler/tests/lowering_tests.cpp`
  (`async int f(bool c) { if (c) return 1; return 2; }` → expects
  `" if (c) co_return 1; co_return 2; "`, T-0001's first criterion);
- a brand-new suite `transpiler/tests/brand_new_tests.cpp` that calls an
  API absent on base.

Procedure:
1. Create a worktree at the merge-base.
2. Copy in the head version of every A/M/R path under `transpiler/tests`,
   `runtime/tests`, and `examples`.
3. `cmake -S overlay -B ob && cmake --build ob -- -k && ctest --test-dir ob --output-junit j.xml`.

Observed:
```
build_rc=0   ctest_rc=8
lexer_tests run | parser_tests run | lowering_tests fail | golden_tests run | task_tests run
executor_tests run | hello_example run | yielding_example run | consumer_example run
```
The new suite is not registered in the base `transpiler/CMakeLists.txt`,
so ctest never sees it. Compiled standalone against the base library, it
fails as expected:
```
$ g++ -std=c++20 -I overlay/transpiler/include -I overlay/transpiler/tests overlay/transpiler/tests/brand_new_tests.cpp ob/transpiler/libasynctp_core.a
brand_new_tests.cpp:3:38: error: 'this_function_does_not_exist_on_base' is not a member of 'asynctp'
```

CTest JUnit statuses, checked with a probe project (CMake 4.3.4):

| Case | `status` | child |
|---|---|---|
| exit 0 | `run` | — |
| exit 1 | `fail` | `<failure message="Failed">` |
| exit 77 with `SKIP_RETURN_CODE 77` | `notrun` | `<skipped message="SKIP_RETURN_CODE=77">` |
| executable missing (test failed to build) | `notrun` | `<skipped message="Unable to find executable">` |
| `DISABLED` | `disabled` | — |

So `notrun` alone is ambiguous. The skip message distinguishes a real
skip from "did not build". This matters for T-0021's clangd tests, which
skip with exit 77 in CI.

## Scope

- In: `tools/ci/fail_on_base.py` and its tests; the `agent tests fail on
  base` job in `agent-gates.yml`.
- Out:
  - Proving the failing test is *meaningful*. A test calling a trivial new
    function would "fail on base" by not compiling. Review, the tester
    agent, and T-0030 evidence markers cover meaning.
  - Overlaying build files: production CMake/Makefile edits are never
    overlaid.
- Touches: `tools/ci/fail_on_base.py`, `tools/ci/tests/test_fail_on_base.py`,
  `.github/workflows/agent-gates.yml`, `tools/ci/README.md`.

## Blueprint

### 1. `tools/ci/fail_on_base.py`

CLI:
`fail_on_base.py --repo <head checkout> --base <merge-base> --head <sha> --branch <head_ref> --workdir <empty dir> [--jobs N]`.
It uses `cilib`.

1. **Ticket type**, from the base ticket (`find_ticket_path` +
   `parse_ticket` at `--base`). If the type is `chore`: print
   `fail_on_base: T-NNNN is a chore; not required` and exit 0. The type
   comes from master, so the agent cannot change it.
2. **Overlay set**: `changed_files` entries with status A, M, R (new
   path), or C whose path matches `transpiler/tests/**`,
   `runtime/tests/**`, or `examples/**`. These are directory globs, so
   they survive T-0017's renames. If the set is empty: exit 1,
   `no test changes in a <type> PR`.
3. **Overlay tree**:
   - `git worktree add --detach <workdir>/overlay <base>`;
   - for each overlay path, write `git show <head>:<path>` into it,
     creating directories;
   - never delete or touch anything else.
4. **Build on base**:
   - `cmake -S overlay -B <workdir>/build -DCMAKE_BUILD_TYPE=Debug`. If
     configure fails, exit 1 with
     `INCONCLUSIVE: base + PR tests does not configure` plus the
     configure log tail. This usually means the tests need build-file
     changes, so owner review is required.
   - `cmake --build <workdir>/build --parallel N -- -k`. Ignore the exit
     code: test targets that fail to build show up as "Unable to find
     executable".
5. **Run**: `ctest --test-dir <workdir>/build --timeout 300 --output-junit <workdir>/junit.xml`.
   Parse it with `xml.etree.ElementTree`. Classify each `testcase`:

   | status / child | Class |
   |---|---|
   | `fail` | **failing** |
   | `notrun` with `skipped@message == "Unable to find executable"` | **failing (does not build on base)** |
   | `notrun` with `skipped@message` starting with `SKIP_RETURN_CODE=` | skipped |
   | `run`, `disabled` | passing / ignored |
   | anything else | **tool error, exit 2** |

6. **New suites unknown to base CMake**: every *added* `*.cpp` directly
   in `transpiler/tests/` or `runtime/tests/` whose stem is not a ctest
   name from step 5. Compile it standalone:
   - transpiler:
     `$CXX -std=c++20 -pthread -I overlay/transpiler/include -I overlay/transpiler/tests <tu> <build>/transpiler/libasynctp_core.a -o <exe>`;
   - runtime: `$CXX -std=c++20 -pthread -I overlay/runtime/include -I overlay/runtime/tests <tu> -o <exe>`.

   A compile failure, or a run exit code that is neither 0 nor 77, is
   **failing**. Exit 0 is passing; 77 is skipped. If `libasynctp_core.a`
   is absent (base layout changed), exit 2 with a message naming the
   expected path, so the library seam is updated deliberately.
7. **Verdict**:
   - At least one failing → exit 0:
     `fail_on_base: OK, failing on base: lowering_tests, standalone:brand_new_tests`.
   - Otherwise exit 1:
     `fail_on_base: no test fails on base. The PR's tests pass without its production change: either they don't exercise it, or the change is behaviour-preserving (then the ticket should be type chore).`
   - Either way, write a step-summary table (test, class, first failure
     line).

### 2. Job in `.github/workflows/agent-gates.yml`

Use the same header and steps as T-0029's `scope` job, with these
differences:
- `name: agent tests fail on base`;
- `timeout-minutes: 30`;
- install `cmake python3`;
- the base-tools check tests for `fail_on_base.py`;
- last step:
  ```yaml
      - name: Tests fail on base
        env: { CXX: g++ }
        run: python3 "$RUNNER_TEMP/base/tools/ci/fail_on_base.py" --repo . --base "$MB" --head "$HEAD_SHA" --branch "$HEAD_REF" --workdir "$RUNNER_TEMP/fob"
  ```

The base-tools worktree (`$RUNNER_TEMP/base`) and the overlay worktree
(`$RUNNER_TEMP/fob/overlay`) are separate on purpose: the gate's own code
is never overwritten by the overlay.

### Invariants

- Product code is untouched.
- The gate executes PR test code against base production code inside the
  read-only, secretless job (design doc P4).
- The "failing tests" seam is `asynctp_core`, the existing test library
  in `transpiler/CMakeLists.txt`. If T-0009 children rename it, step 6
  fails with exit 2 instead of silently passing.

## Acceptance criteria

- [ ] `tools/ci/tests/test_fail_on_base.py` builds a temp git repo with a
      tiny CMake project (a `lib.cpp` exposing `int answer()`, a test
      `t_answer.cpp` that checks it, registered with `add_test`).
      [evidence: tools/ci/tests/test_fail_on_base.py]
  - Head changes `answer()` and the test's expectation → OK, failing
    `t_answer`.
  - Head changes only the test to expect the *current* value → exit 1
    "no test fails on base".
  - Head changes only `lib.cpp` → exit 1 "no test changes".
  - Base ticket type `chore` → exit 0 "not required".
  - A test that calls a function absent on base → classified "does not
    build on base" (ctest `Unable to find executable`).
  - A test returning 77 with `SKIP_RETURN_CODE 77` is *skipped*, not
    failing.
  - An added suite not registered in base CMake that fails to compile →
    `standalone:<name>` failing.
  - A configure failure → INCONCLUSIVE, exit 1.
  - A JUnit `notrun` with an unknown message → exit 2.
  - Skip with a notice if `cmake`/`g++` are absent.
- [ ] Replaying the architect's simulation (Problem section) against the
      real repo reports `lowering_tests` and `standalone:brand_new_tests`
      failing on base; record the output in the Log.
      [evidence: tools/ci/fail_on_base.py]
- [ ] `agent tests fail on base` job present and skipped on human PRs.
      Canary C15 (production-only refactor PR for a `bug` ticket) is red.
      Record the run URL. [evidence: .github/workflows/agent-gates.yml]
- [ ] Added to the required checks of `master`.
- [ ] `tools/ci/README.md` documents the procedure and the
      `asynctp_core` seam.
- [ ] `verify.sh all` passes; `tools selftest` green.

## Notes

- **Cost.** One extra Debug build plus ctest, ~20–40 s on a hosted
  runner, only for agent PRs.
- **Pure-refactor tickets** (T-0011 "Port lower_body…", type `chore`) are
  exempt by type. A refactor filed as `bug`/`feature` will be red. The
  fix is re-typing the ticket on master, not bypassing.
- **T-0017** renames example and fixture files. Its PR's overlay contains
  the renamed files, so base + overlay builds old CMake against new
  names. It probably fails to configure, which reports INCONCLUSIVE.
  T-0017 is type `chore` and exempt anyway.
- **T-0015** moves examples to the launcher. Example sources stay under
  `examples/`, so the overlay globs are unchanged.

## Log

- 2026-10-18 architect: created under T-0025. Simulated the overlay on a scratch clone: `lowering_tests` fails on base for a T-0001-style test, and a new suite fails standalone against `libasynctp_core.a`. Probed CTest JUnit statuses: exit-77 skips and missing executables are both `notrun`, told apart by the skipped message. Ready.
