---
id: T-0017
title: Drop the .async.cpp naming from examples, fixtures, and docs
status: ready
area: cross-cutting
type: chore
priority: P2
depends_on: [T-0006, T-0015]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0006 (header declarations), T-0015 (launcher-based build). Run it after every example-adding ticket that is in flight (T-0001, T-0003, T-0004, T-0006, T-0013), so their examples are renamed too.

## User story

As a developer evaluating dotawait, I want the examples, the consumer
project, and the docs to use plain `main.cpp`/`jobs.hpp` with `async` in
both, so that the repo demonstrates the AUTOMOC-like contract rather than
the old suffix convention.

## Problem

After T-0015 the build no longer cares about the suffix, but the tree still teaches it. Every remaining reference (from `grep -rn '\.async\.'`, excluding `docs/` and `build*/`):

- `examples/hello.async.cpp`, `examples/yielding.async.cpp`, `examples/consumer/src/main.async.cpp`, `examples/consumer/src/jobs/jobs.async.cpp`; `examples/CMakeLists.txt` lines 1-2; `examples/consumer/CMakeLists.txt` lines 24, 27-28.
- `examples/consumer/src/jobs/jobs.hpp` hand-writes `asyncrt::Task<int> download(std::string url, int chunks);` and includes `<asyncrt/task.hpp>`.
- Golden fixtures `transpiler/tests/golden/*.async.cpp`; `golden_tests.cpp:33` (`case_name + ".async.cpp"`); `.claude/skills/golden-test/scripts/golden.sh:14`; `.claude/skills/golden-test/SKILL.md:9,15`.
- `Makefile:22,57`; `cmake/AsyncTranspile.cmake` comments (rewritten by T-0015) and `async_transpile_sources` (deprecated by T-0015).
- `transpiler/src/main.cpp:21` usage text `<input.async.cpp>`.
- `runtime/include/asyncrt/asyncrt.hpp:5` comment.
- `.gitattributes:8-9`.
- `README.md` lines 27, 32-33, 43, 98-107, 116, 159.
- `CLAUDE.md` lines 4, 44, 70, 73.
- `.claude/skills/ticket/templates/ticket.md:13,17`, `.claude/skills/ticket/SKILL.md:27`, `.claude/skills/syntax-feature/SKILL.md:3`.

Also review finding N3. `asynctp in -o /dev/stdout` (recommended in CLAUDE.md) opens stdout with `trunc`. When stdout is redirected to a file, that truncates the file the shell already wrote. The architect lost earlier output this way while reproducing tickets.

## Scope

- In:
  - `git mv` every example and consumer source to `.cpp`;
  - the consumer header declares `async int download(...)`;
  - golden fixtures renamed `<case>.in.cpp`;
  - `asynctp -o -` writes to stdout;
  - remove `async_transpile_sources`;
  - all docs and comments listed above;
  - a rewritten README "Known MVP limitations" (review finding N4).
- Out: any behaviour change in lowering.
- Touches: the files listed above. **`CLAUDE.md` edits require explicit human approval in the PR** (it is the project constitution). Prepare the diff using the wording in step 7.

## Blueprint

1. **Examples and consumer.** `git mv examples/<x>.async.cpp examples/<x>.cpp` for every example. Update `examples/CMakeLists.txt` and the Makefile `EXAMPLES` rule (now `examples/%.cpp`).

   In the consumer:
   - `src/main.async.cpp` becomes `src/main.cpp`;
   - `src/jobs/jobs.async.cpp` becomes `src/jobs/jobs.cpp`;
   - `jobs.hpp` drops `#include <asyncrt/task.hpp>` and declares `async int download(std::string url, int chunks);`. The comment explains that asynctp-cc lowers the header in the build tree.
   - `report.cpp` stays plain (it shows mixing).
   - Update `examples/consumer/CMakeLists.txt` and its comments.
2. **Golden fixtures.** `git mv transpiler/tests/golden/<case>.async.cpp <case>.in.cpp`. Change `golden_tests.cpp` `run_case` to read `<case>.in.cpp`; also `golden.sh` and `golden-test/SKILL.md`.
3. **CLI.** `main.cpp`:
   - `-o -` means write to `std::cout`, binary, no truncation;
   - usage text becomes `usage: asynctp <input.cpp|input.hpp> -o <output|-> [--emit-map <file>] [--line-directives] [--no-line-directives] [--no-runtime-include] [--verbose]`.

   Add a golden-style check that `-o -` output equals the `-o file` output.
4. **CMake.** Remove `async_transpile_sources`. Replace it with a stub that does `message(FATAL_ERROR "async_transpile_sources was removed: sources need no transpile step any more; call target_enable_async(<target>)")`.
5. **Comments.** Update `runtime/include/asyncrt/asyncrt.hpp:5` ("asynctp injects this include into every lowered file that contains an `async` function"; comment only, no runtime code change). Remove the two `.async.cpp` lines from `.gitattributes`.
6. **README rewrite:**
   - intro and Layout: plain `.cpp`/`.hpp`;
   - "Using it": header declarations, laziness;
   - "CMake": from T-0015;
   - new "Editor / LSP" section: a placeholder line pointing at T-0021 until that ships;
   - "Known MVP limitations": re-derive the list from the header comments in `lexer.hpp`, `async_parser.hpp`, `lowering.hpp`, `emitter.hpp`, `launcher.hpp`, so that every limitation documented in a header appears in the README and vice versa (closes N4). Keep the `-O0` symmetric-transfer bullet.
7. **CLAUDE.md** (human-approved diff). Replace:
   - line 4, "lowers `.async.cpp` into real coroutine code", with:
     "lowers any C++ file that uses `async`/`.await` into real coroutine code in the build tree (via the `asynctp-cc` compiler launcher; sources are never modified)";
   - line 44, `build/transpiler/asynctp in.async.cpp -o /dev/stdout --no-line-directives`, with:
     `build/transpiler/asynctp in.cpp -o - --no-line-directives`;
   - line 70, "`tests/golden/<case>.async.cpp` + `<case>.expected.cpp`", with:
     "`tests/golden/<case>.in.cpp` + `<case>.expected.cpp`";
   - line 73, "`examples/*.async.cpp` run as tests", with:
     "`examples/*.cpp` run as tests (built through `asynctp-cc`)".

   And add under "Architecture invariants":
   - "Build integration never writes into the source tree. Lowered and mirrored copies live under the build directory's shadow tree, and `compile_commands.json` must keep listing the user's original files."
   - "Every `.cpp`/`.hpp` of an async-enabled target passes through asynctp: anything GCC accepts in a file without async markers must pass through unchanged."
   - Replace the zero-dependency invariant (D1, approved 2026-10-17) with: "Production code in `transpiler/` and `runtime/` is **zero-dependency**: the standard library plus, for process management in `transpiler/` tools only, the host OS API (POSIX) behind `transpiler/src/platform/`. No Boost, no libclang, no parser generators, no third-party libraries."
8. **Ticket template and skills:** in `.claude/skills/ticket/templates/ticket.md` and `.claude/skills/ticket/SKILL.md`, replace "writing `.async.cpp`" with "writing async C++", and "a minimal `.async.cpp` snippet" with "a minimal C++ snippet using `async`". Same for `syntax-feature/SKILL.md`.

### Invariants

- **Makefile/CMake parity:** both are renamed together (step 1).
- **Transpiler/runtime independence:** the runtime change is a comment only.
- **Other invariants:** unaffected.

## Acceptance criteria

- [ ] `grep -rn '\.async\.' --exclude-dir=build* --exclude-dir=docs .` returns nothing, except a historical mention in a CHANGELOG/README migration note if one is added.
- [ ] `examples/consumer` builds standalone (`cmake -S examples/consumer -B build-consumer && cmake --build build-consumer && ./build-consumer/consumer_app`, exit 0). `jobs.hpp` contains `async int download(` and no `asyncrt` include.
- [ ] `golden_tests` passes with the `.in.cpp` names. A new check covers `-o -` output parity.
- [ ] Calling `async_transpile_sources` fails configuration with the documented message. Test it with a ctest using `PASS_REGULAR_EXPRESSION`, like T-0015's consumer check.
- [ ] README: no suffix references. "Known MVP limitations" matches the header comments one-to-one (the reviewer checks the list against each header).
- [ ] CLAUDE.md diff per step 7 is included in the PR and approved by the human before merge.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Log

- 2026-10-17 architect: created under epic T-0009; enumerated every `.async.` reference with grep; reproduced N3 (`-o /dev/stdout` truncating redirected output); ready.
- 2026-10-17 architect: D1 accepted. Add the D1 wording to the CLAUDE.md diff in step 7: "Production code in `transpiler/` and `runtime/` is zero-dependency: the standard library plus, for process management in `transpiler/` tools only, the host OS API (POSIX) behind `transpiler/src/platform/`. No third-party libraries." D2 accepted (remove async_transpile_sources here). The CLAUDE.md edits stay in this ticket, human-approved.
