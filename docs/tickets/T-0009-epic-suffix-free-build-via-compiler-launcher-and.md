---
id: T-0009
title: Epic: suffix-free build via compiler launcher and LSP proxy
status: proposed
area: cross-cutting
type: feature
priority: P1
depends_on: []
created: 2026-10-17
---

> **Tracking ticket. Not implementable on its own.** It stays `proposed`
> and is closed (`done`) by the architect when every child ticket is
> `done` or `rejected`. The ticket format has no epic type, so this file
> plays that role. Design sketch: `docs/sketches/review-latest.html`.

## User story (the user's new product contract)

"If a function is declared with the `async` keyword, automatically
transform it to be C++-coroutine compatible and tack on our internal
headers. All modification and transformation is performed on the built
artifact, not the actual files. The current requirement that files be named
`file.async.cpp` is cumbersome and against the purpose of the project;
transpiling should imitate what Qt's moc/AUTOMOC does. Our transpiler means
nothing if it only serves compilation: developers need LSP /
autocompletion, otherwise their compiler and LSP servers complain when
integrating our libs. LSP and autocompletion support is mandatory."

Decisions already made by the user:
- **LSP proxy.** `asynctp-lsp` sits between the editor and clangd and maps positions through a source map.
- The surface syntax (`async`, `.await`) is unchanged.
- Rejected alternatives: macro-only shim; `ASYNC()`/`AWAIT()` macros.

## Problem (what the contract change exposes; every item reproduced on 2026-10-17)

| Finding | Evidence (asynctp / GCC 15.2 / CMake 4.3.4) | Child |
|---|---|---|
| Only `*.async.cpp` is transpiled; compile DB lists generated files | `compile_commands.json` `"file": ".../asynctp_generated/hello_example/hello.cpp"` | T-0014, T-0015 |
| `std::async` in a plain file breaks the build once every TU passes through | `stdasync.cpp:2:25: error: could not find a function name…`; prototype launcher build failed on `plain.cpp` | T-0004 |
| One digit separator / `#if 0` apostrophe / `#define … don't` silently disables lowering for the rest of the file (rc=0) | output identical to input, `async` left in | T-0010 |
| Digit separator inside an async body is a hard error | `1:43: error: unterminated string or char literal` | T-0010, T-0011 |
| Lowering errors are body-relative (H2) | reported `3:12`, real `6:12` | T-0011 |
| `.await` operand: strings, chains, template args, keywords | `g(")")co_await ()`, `co_await (a())co_await (.b())`, `get<int>co_await (())`, `co_await (co_yield x)` | T-0011, T-0003 |
| Lambda after `else`/`do` gets `co_return` | `else [c]{ co_return 1; }()` | T-0005 |
| Line drift for multi-line signature (M3), column drift, BOM, `#line` escaping | `m3b.cpp:3:13` vs line 6; col 53 vs 37; `'﻿int' does not name a type`; `unknown escape sequence: '\i'` | T-0012, T-0016 |
| No-await `async void` is not a coroutine (H1) | compiles with `-Wreturn-type`, runs to `Illegal instruction` (exit 132) | T-0013 |
| Declarations / qualified names / unbounded name search (H3) | `async int f(int);` error; `struct Foo { async int bar(); };` gives garbage `::asyncrt::Task<int bar(); };` | T-0006, T-0002 |
| Half-typed buffers produce no output | `half.cpp:1:27: error: unexpected end of file while matching delimiters`, no file written | T-0020 |
| No LSP at all; clangd not installed here | `which clangd`: nothing; apt candidate 21.1.6 | T-0018 to T-0024 |

## Architecture (validated by experiment; details in each child)

1. **Build: compiler launcher, not suffix and not custom commands.**
   `asynctp-cc` runs as `CXX_COMPILER_LAUNCHER`; for plain Makefiles, `CXX = asynctp-cc … -- g++`.
   - **Verified:** CMake does **not** put the launcher into `compile_commands.json` (Unix Makefiles generator; Ninja not installed here, so it is an acceptance item in T-0015). clangd sees original files and flags.
   - Custom commands without the suffix were rejected for these reasons:
     - the compile DB would list generated files (observed today);
     - a configure-time scan goes stale;
     - headers are invisible to them.
2. **Headers: shadow tree mirroring every reached non-system header.**
   - Run a per-TU `-MM` scan with the user's own flags.
   - Lower the files that have markers; copy the others with `#line 1`.
   - Insert a shadow twin before every `-I`/`-iquote`; rewrite the depfile back to original paths; post-check for original-path bypass.
   - **Verified:**
     - GCC resolves quoted includes against the physical file;
     - shadowing only async headers breaks their sibling includes (`fatal error: detail.hpp`);
     - mirror-all works for quoted and angle includes;
     - depfile rewrite gives correct rebuilds;
     - the `-M` scan costs ~13% of a compile.
3. **Source map** (T-0012) is the shared foundation: build `#line` correctness (line-preserving lowering), column remap (T-0016), and all LSP mapping (T-0022/T-0023). It subsumes H2 (done in T-0011) and M3.
4. **Shared tokenizer first** (T-0010 → T-0004 → T-0011). With every file passing through, lexing robustness is a build-correctness issue. `std::async` / `std::launch::async` are never markers (T-0004 rule 1).
5. **Tolerant mode** (T-0020) for the LSP path only. The build path stays strict (fail loudly); enforced by API (`Mode`) and a launcher test.
6. **`asynctp-lsp`** (T-0021 to T-0024): zero-dependency JSON/JSON-RPC (T-0019), lives in `transpiler/`, never includes `runtime/`. Staged: transparent proxy → lowered buffers + diagnostics → request mapping → headers. The header mechanism for clangd is unverified, so a spike comes first (T-0018).

## Invariant check

| Invariant (CLAUDE.md) | Risk under the new contract | How the epic keeps it |
|---|---|---|
| transpiler/ and runtime/ independent | the launcher injects `#include <asyncrt/asyncrt.hpp>`; tests need the runtime path | Injection is text (as today). The include path comes from the user's flags; CMake links `asyncrt` in `cmake/`. Tests receive the runtime dir as an argument. `asynctp-lsp` relies on `compile_commands.json`. |
| Zero-dependency (std lib only) | the launcher must run the compiler; the proxy must spawn clangd with pipes | Launcher: `std::system` with POSIX-sh quoting (ISO only). Proxy: POSIX `pipe/fork/exec` behind `transpiler/src/platform/`, per **D1 = (b)** (decided 2026-10-17; the CLAUDE.md wording lands with T-0017). |
| Runtime header-only | none | unchanged |
| Text-island rewriter; passthrough byte-for-byte | mirrored files get a `#line` prefix; tolerant mode blanks orphan `async` | Prefix and BOM relocation exist only in build artifacts, and the source tree is never written (asserted by test). Tolerant output is never compiled. |
| Parse failures loud | tolerant mode is lenient by design | Strict/Tolerant split by API. The launcher and CLI use Strict only (tested). |
| Lazy Task + symmetric transfer | H1 non-coroutine bodies run eagerly (UB) | T-0013 restores laziness; tested under ASan. |
| Makefile mirrors CMake | new binaries (`asynctp-cc`, `asynctp-lsp`) and ~9 new suites | Each child lists both build edits in its acceptance criteria. |

## Children, in dependency order

The tree stays green after each one.

| # | Ticket | Status | Depends on | Scope |
|---|---|---|---|---|
| 0 | T-0001 | ready | none | braceless `return` → `co_return` (already in Jira as SCRUM-8; land first) |
| 1 | T-0010 | ready | none | shared tokenizer; port marker scan + parser |
| 2 | T-0004 | ready | T-0010 | classify `async` markers; `std::async` untouched (P1) |
| 3 | T-0011 | ready | T-0010 | `lower_body` on tokens; delete `SourceCursor`; H2 |
| 4 | T-0003 | ready | T-0011 | `.await` operand grammar (chains, templates, keywords) |
| 5 | T-0005 | ready | T-0011 | lambda-start keyword table |
| 6 | T-0012 | ready | T-0011 | source map, line-preserving lowering, BOM, `#line` escaping |
| 7 | T-0013 | ready | T-0011 | every async fn is a coroutine (H1) |
| 8 | T-0006 | ready | T-0010, T-0004 | declarations, qualified names, bounded search (H3) |
| 9 | T-0002 | ready | T-0006 | `noexcept`, trailing return, member qualifiers |
| 10 | T-0014 | ready | T-0010, T-0004, T-0012, T-0006 | `asynctp-cc` launcher |
| 11 | T-0015 | ready | T-0014 | CMake `target_enable_async` / `ASYNCTP_AUTO`, Makefile via launcher |
| 12 | T-0016 | ready (P3) | T-0012, T-0014 | remap GCC diagnostic columns |
| 13 | T-0017 | ready | T-0006, T-0015 | drop `.async.cpp` naming; docs; CLAUDE.md diff (human-approved) |
| L0 | T-0018 | ready (spike) | D4 (clangd installed) | verify clangd behaviour; write `docs/design/lsp-proxy.md` |
| L1 | T-0019 | ready | none | JSON + JSON-RPC framing |
| L2 | T-0020 | ready | T-0012 (+T-0003, T-0006 recommended) | tolerant lowering mode |
| L3 | T-0021 | ready | T-0019 | transparent clangd proxy + discovery + editor setup (D1 resolved; spike no longer required) |
| L4 | T-0022 | proposed | T-0021, T-0020, T-0012, T-0018 | lowered buffers + diagnostics mapping |
| L5 | T-0023 | proposed | T-0022, T-0018 | completion/hover/navigation mapping |
| L6 | T-0024 | proposed | T-0018, T-0022, T-0006 | lowered headers for clangd |

- Tracks 1-9 (transpiler) and L1 can proceed in parallel with the spike.
- Not part of this epic but still valid: T-0007 (join; runtime-only), and T-0008 (async lambdas; must extend T-0004's rule).

## Decisions (made by the human, 2026-10-17)

| # | Decision | Outcome | Effect |
|---|---|---|---|
| D1 | Zero-dependency vs OS APIs | **(b)**: POSIX process/pipe APIs allowed, isolated in `transpiler/src/platform/`; still no third-party libraries | Unblocks T-0021 (now ready). The CLAUDE.md wording ships with T-0017, not before. Until then the D1 record lives here and in T-0017. |
| D2 | `async_transpile_sources` | Deprecate in T-0015, remove in T-0017 (as recommended) | No change to the tickets |
| D3 | AUTOMOC-like switch | Both `target_enable_async` and `ASYNCTP_AUTO` (as recommended) | T-0015 as written |
| D4 | clangd | The human installs clangd 21.1.6 via apt; real-clangd tests skip with exit 77 when it is absent | T-0018 can run; T-0021 to T-0024 keep the 77 skip |
| D5 | Header shadow scheme | Mirror every reached non-system header (as recommended) | T-0014 as written |
| D6 | Build approach | Compiler launcher (as recommended) | T-0014/T-0015 as written |
| D7 | SCRUM-8 / T-0001 | Keep it as-is and ship first; the human links it to this epic in Jira | No change to T-0001's scope |
| D8 | Scheduling | **(b)**: the main session teaches `ticket.py` and the ship skill to respect dependencies. Each ticket's hard dependencies are in front matter as `depends_on: [T-…]` (or `[]`), placed right after `priority:`; the body "Dependency notes"/"Depends on" prose is explanation only | Architect does not touch the tooling |

## Proposed CLAUDE.md / README changes

The architect writes only under `docs/`; the edits happen in T-0017, which carries the exact wording.

- CLAUDE.md line 4, line 44 (`-o -` instead of `-o /dev/stdout`, which truncates redirected output: N3), line 70 (golden `<case>.in.cpp`), line 73 (examples `*.cpp` through `asynctp-cc`).
- Two new invariants: the source tree is never written, and compile_commands lists original files; anything GCC accepts without markers passes unchanged.
- D1 wording, if approved.
- README: intro, Layout, "Using it" (headers, laziness), CMake (T-0015), new "Editor / LSP" section (T-0021), and "Known MVP limitations" re-derived from header comments (N4).
- Also `.claude/skills/{ticket,golden-test,syntax-feature}` and the ticket template mention `.async.cpp`.

## Acceptance criteria

- [ ] Every child ticket is `done` or `rejected`.
- [ ] End-to-end proof:
  - `examples/consumer` uses plain `main.cpp`, `jobs.cpp`, and a `jobs.hpp` declaring `async int download(...)`;
  - it builds via CMake and via a plain Makefile recipe;
  - its `compile_commands.json` lists those original files.
- [ ] End-to-end proof (LSP): with real clangd, opening the consumer's `main.cpp` through `asynctp-lsp` gives zero diagnostics; completion after `jobs::download(a, 1).await` is mapped correctly; go-to-definition reaches `jobs.hpp`.
- [ ] README and CLAUDE.md updated per T-0017 (human-approved).
- [ ] `verify.sh all` passes on the final tree.

## Log

- 2026-10-17 architect: created from the user's new product contract. Validated the launcher, compile_commands, shadow-header, depfile and DEFER mechanisms by experiment (CMake 4.3.4, GCC 15.2; no ninja, no clangd). Re-triaged T-0001 to T-0008; created T-0010 to T-0024; sketch at docs/sketches/review-latest.html.
- 2026-10-17 architect: recorded the human's decisions D1-D8 (D1=b POSIX in platform/, D2/D3/D5/D6/D7 as recommended, D4 human installs clangd with exit-77 skips, D8=b tooling by main session). Added `depends_on: [...]` front matter (hard dependencies only) to T-0001 to T-0024. T-0021 moved to ready (depends_on: [T-0019]). T-0022/T-0023/T-0024 stay proposed pending spike answers Q2/Q5, Q3/Q4/Q6 and Q1 respectively.
