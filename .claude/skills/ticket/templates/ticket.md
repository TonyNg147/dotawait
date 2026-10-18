---
id: {{id}}
title: {{title}}
status: proposed
area: {{area}}
type: {{type}}
priority: {{priority}}
depends_on: []
created: {{created}}
---

## User story

As a C++ developer writing `.async.cpp`, I want … so that ….

## Problem

What breaks or is missing today. Include a minimal `.async.cpp` snippet and
the current output or error (run `build/transpiler/asynctp`; don't guess).

## Scope

- In: …
- Out: … (and why)
- Touches: `transpiler/` | `runtime/` | `cmake/` | `examples/` | docs

## Blueprint

Ordered steps for the developer, naming files and functions. Note which
invariant from CLAUDE.md is at risk and how the design keeps it.

1. …

## Acceptance criteria

- [ ] New/updated unit tests in `<suite>` covering: …
- [ ] Golden fixture `<case>` (if the CLI output changes)
- [ ] Failure path reported as `ParseError` with line/col (if applicable)
- [ ] README "Known MVP limitations" and the header comment updated
- [ ] Makefile and CMake both build any new file
- [ ] `verify.sh all` passes

## Notes

Open questions, alternatives considered, links to the C++ standard / cppreference.

## Log

Append-only. `YYYY-MM-DD role: what happened`.
