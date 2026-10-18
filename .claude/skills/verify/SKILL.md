---
name: verify
description: Build and test dotawait (CMake + ctest, Makefile parity, ASan/UBSan, standalone consumer project) through one script. Use after any code change, before marking a ticket in-review/done, or when asked to run, build, or check the tests.
---

# Verify

Run from the repo root:

```sh
.claude/skills/verify/scripts/verify.sh quick   # inner loop: build/ + ctest minus consumer_example
.claude/skills/verify/scripts/verify.sh full    # + consumer_example + `make BUILD=build-make test`
.claude/skills/verify/scripts/verify.sh asan    # build-asan/ with -fsanitize=address,undefined
.claude/skills/verify/scripts/verify.sh all     # full + asan (gate for done)
```

| Gate | Required mode |
|---|---|
| While iterating | `quick` |
| Ticket -> `in-review` | `full` |
| Ticket -> `done` | `all` |

## Reading failures

- `golden_tests` mismatch: the expected/actual are printed. Reproduce one case
  with `.claude/skills/golden-test/scripts/golden.sh <case>` (see the
  `golden-test` skill) instead of re-running the whole suite.
- `consumer_example` fails but everything else passes: the break is in
  `cmake/AsyncTranspile.cmake` or in the public include layout, so look at how an
  outside project consumes the repo, not at the transpiler logic.
- Makefile step fails but CMake passes: a new source/test was added to CMake
  only. Add it to the Makefile too (CLAUDE.md invariant).
- ASan/UBSan report: treat as a real bug even if the plain build passes.
  Coroutine frame lifetime (a `Task` destroyed while its handle is still
  queued or awaited) is the usual suspect in `runtime/`.
- Deep `.await` chain stack overflow only at `-O0`: known GCC behavior
  (symmetric transfer is O(1) stack only from `-O1`); see
  `runtime/tests/task_tests.cpp` before "fixing" it.

Report the mode run and its final line (`verify <mode>: OK` or the failing
step) when you summarize work.
