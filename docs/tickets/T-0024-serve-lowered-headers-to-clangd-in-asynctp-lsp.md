---
id: T-0024
title: Serve lowered headers to clangd in asynctp-lsp
status: proposed
area: transpiler
type: feature
priority: P1
depends_on: [T-0018, T-0022, T-0006]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0018 (Q1 decides the mechanism), T-0022, T-0006 (header declarations).

## User story

As a C++ developer, I want clangd to understand `async int download(...);`
in `jobs.hpp` when I edit `main.cpp`, and to jump into `jobs.hpp` at the
right place, so that async APIs split across headers are first-class in
the editor too.

## Problem

clangd reads included headers from disk, so it sees the *original*,
unlowered headers even when the main file is lowered. Experiments with GCC
(T-0014 evidence) show quoted includes resolve against the physical file's
directory. clangd keeps the main file's original URI, so it would find the
original `jobs.hpp`.

## Candidate mechanisms (the spike picks one)

- (a) `didOpen` every reachable project header that has markers, with lowered text, and keep them in sync on disk change (`workspace/didChangeWatchedFiles`) and on editor edits. Needs: does clangd use open-document content for includes, including in the preamble?
- (b) A proxy-generated compile database adding `-ivfsoverlay` that maps original header paths to lowered copies in a cache dir.
- (c) Shadow URIs and a generated compile database, reusing T-0014's shadow layout.

Whichever is chosen:
- the *reachable header set* is found the same way as in T-0014: compiler `-MM` with the file's compile command, or clangd's own include info if the spike shows it can be queried;
- responses pointing into a lowered header are mapped through that header's `SourceMap`;
- nothing is written to the source tree.

## Acceptance criteria (draft)

- [ ] With the consumer example (after T-0017: `jobs.hpp` declares `async int download(...)`), opening `main.cpp` through the proxy gives zero diagnostics from real clangd. The test skips with 77 if clangd is absent.
- [ ] Go-to-definition on `download` lands on the original line/col in `jobs.hpp`.
- [ ] Editing `jobs.hpp` in the editor (unsaved) updates `main.cpp`'s diagnostics.
- [ ] `verify.sh all` passes.

## Notes (open questions)

D1 no longer blocks this ticket (resolved). What still blocks `ready` is spike T-0018 **Q1**: which of (a) open-document overlay, (b) `-ivfsoverlay`, or (c) shadow URIs makes clangd parse against lowered headers, including in the preamble, and in which coordinates go-to-definition answers. The cost of the reachable-header scan (per keystroke vs per save) must also be measured in the spike. Until then this ticket must not be implemented.

## Log

- 2026-10-17 architect: created under epic T-0009; proposed pending T-0018.
- 2026-10-17 architect: D1 resolved (POSIX allowed in transpiler/src/platform/; T-0021 now ready). Still proposed: the header mechanism depends entirely on spike answer Q1.
