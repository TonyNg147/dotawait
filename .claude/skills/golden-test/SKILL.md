---
name: golden-test
description: Add, inspect, or update an asynctp golden (end-to-end CLI output) fixture in transpiler/tests/golden/. Use when a change alters transpiler output, when golden_tests fails, or when a ticket's acceptance criteria name a golden fixture.
---

# Golden fixtures

A golden case is a pair in `transpiler/tests/golden/`:
`<case>.async.cpp` (input) and `<case>.expected.cpp` (exact `asynctp` output
with `--no-line-directives`, runtime include on). `golden_tests` runs the real
binary on each **registered** case and compares the bytes exactly.

## Adding a case

1. Write `transpiler/tests/golden/<case>.async.cpp`. Keep it small and focused
   on the behavior under test, but realistic: it should be valid C++ once
   lowered.
2. Look at the output:
   `.claude/skills/golden-test/scripts/golden.sh <case>`.
   With no expectation file yet, the script prints the current output.
3. Read the output line by line. Check that it is the *intended* lowering
   (correct `co_await (...)` operand, `co_return` placement, untouched
   passthrough text and nested lambdas). Then accept it:
   `golden.sh <case> --accept`.
4. Register the case in `transpiler/tests/golden_tests.cpp` `main()`:
   `run_case(asynctp_path, golden_dir, "<case>");`. The script warns if you
   forget. The Makefile runs the same binary, so it needs no change.
5. Prove the output compiles: `g++ -std=c++20 -fsyntax-only -Iruntime/include`
   on the expected file (add any declarations it needs to the input).

## Updating after an intentional output change

Run `golden.sh <case>` to see the unified diff. Every changed line must be
explained by the ticket. Only then run `--accept`. If a diff shows up in a
case the ticket didn't mention, that's a regression. Don't accept it.
