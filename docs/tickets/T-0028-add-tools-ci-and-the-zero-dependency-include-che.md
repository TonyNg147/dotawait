---
id: T-0028
title: Add tools/ci and the zero-dependency include check
status: ready
area: cross-cutting
type: chore
priority: P2
depends_on: [T-0026]
created: 2026-10-18
---

Epic: T-0025. Design doc: `docs/design/agent-pipeline-security.md` §4, §10.

This ticket touches `.github/`, so it is a human (or local `/ship`) ticket,
never an agent-tracker run.

## User story

As a maintainer, I want CI to reject any production `#include` that breaks
the zero-dependency or transpiler/runtime-independence invariants, with
`file:line`. Today a reviewer has to spot such an include by eye. I also
want a small, tested home for the gate scripts that T-0029 to T-0031 add.

## Problem

- No automated check exists for the CLAUDE.md invariants "zero-dependency"
  and "`transpiler/` and `runtime/` are independent". A
  `#include <boost/…>` or `#include <asyncrt/task.hpp>` in `transpiler/src`
  would compile on a machine that happens to have the header, and nothing
  would flag it.
- Decision D1 (T-0009) allows POSIX headers in `transpiler/src/platform/`
  only. T-0021 enforces that with a one-off `grep` criterion. Nothing keeps
  it true afterwards.
- Current production includes, enumerated on commit `2a7c317` with
  `grep -rnE '^\s*#\s*include' transpiler/src transpiler/include runtime/include`:
  - only standard headers: `<algorithm> <array> <cctype> <coroutine>
    <cstddef> <deque> <exception> <fstream> <iostream> <optional>
    <sstream> <string> <string_view> <utility> <variant> <vector>`;
  - quoted includes that resolve inside their own component:
    `"asynctp/*.hpp"`, `"ast.hpp"`, `"async_parser.hpp"`,
    `"executor.hpp"`, `"task.hpp"`, `"yield.hpp"`.

  The tree is clean, so the check can land green.
- The emitter contains the text `#include <asyncrt/asyncrt.hpp>` inside a
  string literal (`transpiler/src/emitter.cpp:27`,
  `out += "#include <asyncrt/asyncrt.hpp>\n";`) and in a comment
  (`emitter.hpp:19`). A naive grep would flag both. The check must only
  match real directives.

## Scope

- In:
  - `tools/ci/` (Python 3.9+, **standard library only**) with
    `cilib.py`, the shared helpers T-0029 to T-0031 build on;
  - `check_includes.py`;
  - unittest suites;
  - `tools/ci/README.md`;
  - `.github/workflows/policy.yml` with two jobs: `include check` and
    `tools selftest`.
- Out: the agent gates themselves (T-0029, T-0030, T-0031). Wiring the
  check into `verify.sh` or ctest (see Notes, decision D-C).
- Touches: `tools/ci/**` (new), `.github/workflows/policy.yml` (new),
  `CLAUDE.md` is **not** touched (Notes), and `README.md` (one line under
  "Building this repo").

## Blueprint

### Placement and invariants

- `tools/ci/` is neither `transpiler/` nor `runtime/`. CLAUDE.md's
  zero-dependency rule binds only their production code, but this ticket
  keeps the same spirit: stdlib-only Python, the same choice as
  `.claude/skills/ticket/scripts/ticket.py`.
- Nothing in `transpiler/`, `runtime/`, `cmake/`, `examples/`, the
  Makefile, or CMake references `tools/`. Independence is untouched, and
  the C++ build does not gain a Python dependency.
- The Makefile/CMake parity invariant covers C++ suites. These Python
  suites run in CI (`tools selftest`) and by hand, and are documented in
  `tools/ci/README.md`.

### 1. `tools/ci/cilib.py` (shared helpers; every function unit-tested)

```python
def git(repo: str, *args: str) -> str          # subprocess.run(["git", "-C", repo, *args], check=True, text=True, capture_output=True).stdout
def merge_base(repo, base, head) -> str
def changed_files(repo, base, head) -> list[Change]
    # parses `git diff -M --name-status -z base head`
    # Change(status: 'A'|'M'|'D'|'R'|'C'|'T', path: str, old_path: str | None)
def numstat(repo, base, head) -> list[tuple[int|None, int|None, str]]   # None = binary
def added_lines(repo, base, head, path) -> list[str]   # '+' lines of `git diff -U0`
def show(repo, ref, path) -> str | None        # `git show ref:path`, None if absent
def ticket_id_from_branch(branch) -> str | None   # fullmatch r"agent/(T-\d{4})" -> "T-NNNN"
def find_ticket_path(repo, ref, ticket_id) -> str | None  # docs/tickets/T-NNNN-*.md via `git ls-tree --name-only ref docs/tickets/`
def parse_ticket(text) -> Ticket
    # front matter with the same regex as ticket.py (r"\A---\n(.*?)\n---\n", re.S),
    # key: value lines; depends_on parsed like ticket.py deps();
    # sections: dict of "## Heading" -> body text
def glob_match(pattern, path) -> bool          # '**' crosses '/', a leading or inner '**/' also matches zero dirs; '*' and '?' do not cross '/'; no other syntax
def annotate(level, msg, file=None, line=None) # prints "::error file=…,line=…::msg" (GitHub) and "file:line: error: msg"
def summary(markdown: str)                     # appends to $GITHUB_STEP_SUMMARY when set
```

### 2. `tools/ci/check_includes.py`

- CLI: `python3 tools/ci/check_includes.py [--root .]`.
- Scans **production** files only: `transpiler/src/**`,
  `transpiler/include/**`, `runtime/include/**`, with extensions `.cpp .cc
  .hpp .h .hh .ipp .tpp`. It never scans `*/tests/**`, `examples/`, or
  `build*/`.

**Parsing.**
- Join backslash-continued lines.
- Remove `/* … */` and `//` comments with a small state machine that skips
  string and char literals, including raw strings `R"d( … )d"`.
- Then match directives only at line start:
  `^\s*#\s*(include|include_next|import)\b(.*)$`.

**Rules.** Each violation is reported as `path:line: error: <rule>: <msg>`
plus a GitHub annotation.

| Rule | Condition | Message hint |
|---|---|---|
| I1 | `<name>`, where `name` is in `STD_HEADERS` (C++20 standard library + C-compat `<cxxx>`; list below) | allowed |
| I2 | `<name>` in `POSIX_HEADERS`, **and** the file is under `transpiler/src/platform/` | allowed (D1) |
| I3 | `<asynctp/…>` / `"asynctp/…"` from a `transpiler/` file that resolves to `transpiler/include/…`; `<asyncrt/…>` / `"asyncrt/…"` from a `runtime/` file that resolves to `runtime/include/…` | allowed |
| I4 | `asyncrt/` included from `transpiler/`, or `asynctp/` from `runtime/` | `independence: transpiler/ must not include runtime/` (or the reverse) |
| I5 | Quoted include that resolves (first relative to the including file's directory, then the component's `include/`) to an existing file **inside the same component root** | allowed. Anything else fails with `quoted include does not resolve inside <component>/` (this catches `"../../runtime/…"` and `"string"`). |
| I6 | Any other `<name>`, including C headers like `<stdio.h>` | `zero-dependency: <name> is not a C++20 standard header`; for POSIX names outside `platform/`, add `POSIX headers are allowed only in transpiler/src/platform/ (D1)` |
| I7 | `#include` whose operand is neither `<…>` nor `"…"` (macro include), or `#include_next` / `#import` | `computed or non-standard include directive` |

`STD_HEADERS` (C++20, [headers] tables 24 and 25; `<ciso646>`,
`<ccomplex>`, `<cstdalign>`, `<cstdbool>`, `<ctgmath>` are deliberately
absent because C++20 removed or deprecated them):
`algorithm any array atomic barrier bit bitset charconv chrono codecvt
compare complex concepts condition_variable coroutine deque exception
execution filesystem format forward_list fstream functional future
initializer_list iomanip ios iosfwd iostream istream iterator latch limits
list locale map memory memory_resource mutex new numbers numeric optional
ostream queue random ranges ratio regex scoped_allocator semaphore set
shared_mutex source_location span sstream stack stdexcept stop_token
streambuf string string_view strstream syncstream system_error thread
tuple type_traits typeindex typeinfo unordered_map unordered_set utility
valarray variant vector version cassert cctype cerrno cfenv cfloat
cinttypes climits clocale cmath csetjmp csignal cstdarg cstddef cstdint
cstdio cstdlib cstring ctime cuchar cwchar cwctype`

`POSIX_HEADERS` (enough for T-0021's process layer; extending it is an
owner-reviewed change to this file):
`unistd.h fcntl.h poll.h signal.h spawn.h errno.h sys/types.h sys/wait.h
sys/stat.h sys/select.h mach-o/dyld.h` (the last one is for T-0021's
macOS `_NSGetExecutablePath`).

**Output.** Exit 1 if any violation. Otherwise exit 0 and print
`include check: <F> files, <N> includes OK`.

### 3. Tests: `tools/ci/tests/test_cilib.py`, `tools/ci/tests/test_check_includes.py`

- `unittest` only.
- Each case builds a throwaway tree in `tempfile.TemporaryDirectory()`.
  The git helpers `git init` a temp repo and make commits with
  `-c user.name=t -c user.email=t@t`.
- Run with `python3 -m unittest discover -s tools/ci/tests -t . -v` from
  the repo root.

### 4. `.github/workflows/policy.yml`

```yaml
# Repo-wide policy checks that are not part of the C++ build.
name: policy
on:
  pull_request:
  push:
    branches: [master]
permissions:
  contents: read
jobs:
  include-check:
    name: include check
    runs-on: ubuntu-24.04          # host python3 + git suffice; no compiler needed
    timeout-minutes: 5
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with: { persist-credentials: false }
      - run: python3 tools/ci/check_includes.py
  tools-selftest:
    name: tools selftest
    runs-on: ubuntu-24.04
    timeout-minutes: 5
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with: { persist-credentials: false }
      - run: python3 -m unittest discover -s tools/ci/tests -t . -v
```

After it has run once, add `include check` and `tools selftest` to the
`master: required CI` ruleset (T-0026 step 2).

### 5. Docs

- `tools/ci/README.md`: purpose, the stdlib-only rule, how to run each
  script and the tests, and "gate scripts run from the merge-base copy in
  agent-gates.yml, so changes here take effect for PRs opened after they
  merge".
- `README.md` "Building this repo": one line pointing to
  `tools/ci/README.md`.

## Acceptance criteria

- [ ] `python3 tools/ci/check_includes.py` on the current tree exits 0 and
      prints `include check: … OK`. [evidence: tools/ci/check_includes.py]
- [ ] `test_check_includes.py` covers each case, and each case fails
      if its rule is removed. [evidence: tools/ci/tests/test_check_includes.py]
  - `#include <vector>` and `#  include <string_view>` allowed;
  - `<boost/asio.hpp>` → I6;
  - `<stdio.h>` → I6;
  - `<unistd.h>` in `transpiler/src/lexer.cpp` → I6 with the D1 hint;
  - `<unistd.h>` and `<sys/wait.h>` in `transpiler/src/platform/process_posix.cpp`
    allowed;
  - `<unistd.h>` in `transpiler/include/asynctp/platform/process.hpp` → I6
    (POSIX stays out of headers);
  - `<asyncrt/asyncrt.hpp>` in `transpiler/src/x.cpp` → I4;
  - `"asynctp/lexer.hpp"` in `runtime/include/asyncrt/x.hpp` → I4;
  - `"../../runtime/include/asyncrt/task.hpp"` from `transpiler/src` → I5;
  - `"ast.hpp"` sibling allowed;
  - `"missing.hpp"` → I5;
  - `#include FOO_H` → I7;
  - `#include_next <vector>` → I7;
  - `// #include <boost/x.hpp>` ignored;
  - a `/*` … `#include <boost/x.hpp>` … `*/` block spanning lines ignored;
  - `out += "#include <asyncrt/asyncrt.hpp>\n";` ignored;
  - a raw string with a line starting `#include <boost/x>` ignored;
  - a backslash-continued `#include \` + `<boost/x.hpp>` → I6;
  - files under `transpiler/tests/` not scanned.
- [ ] `test_cilib.py` covers each helper against a temp git repo.
      [evidence: tools/ci/tests/test_cilib.py]
  - `changed_files` reports A/M/D and an R with `old_path`, for a
    rename-plus-edit at ≥ 50% similarity;
  - `numstat` returns `None` for a binary file;
  - `added_lines` returns only `+` lines;
  - `show` returns `None` for an absent path;
  - `ticket_id_from_branch`: `agent/T-0042` → `T-0042`; `agent/T-42`,
    `agent/T-0042-x`, `feature/T-0042` → `None`;
  - `parse_ticket` on a copy of a real ticket (T-0001) yields
    `status: ready`, `depends_on: []`, and an `Acceptance criteria`
    section;
  - `glob_match`: `transpiler/tests/**` matches `transpiler/tests/golden/a.cpp`;
    `*.md` does not match `docs/a.md`; `**/CODEOWNERS` matches both
    `CODEOWNERS` and `.github/CODEOWNERS`.
- [ ] `.github/workflows/policy.yml` exists as in the Blueprint. On a no-op
      PR both jobs are green. On a throwaway PR adding
      `#include <boost/asio.hpp>` to `transpiler/src/lexer.cpp`,
      `include check` is red with an annotation on that line (run URL in
      the Log). [evidence: .github/workflows/policy.yml]
- [ ] Both checks added to the required-checks ruleset.
- [ ] `tools/ci/README.md` written; README line added. [evidence: tools/ci/README.md]
- [ ] Nothing outside `tools/`, `.github/`, and `README.md` changed
      (`git diff --stat`).
- [ ] `verify.sh all` passes (no C++ change expected).

## Notes

- **D-C (decision for the human): should the include check also run in
  `verify.sh`?** Recommended: no, CI only for now. Adding it to
  `verify.sh` makes Python a prerequisite of every local verify and edits
  `.claude/`; CI already blocks the merge. Revisit if developers keep
  tripping it in CI.
- **CLAUDE.md** currently says "zero-dependency: standard library only".
  D1's wording lands with T-0017. Until then, this check is stricter than
  the text only in allowing POSIX in `platform/`, which is the recorded
  D1 decision.
- The `cilib.py` API is a contract for T-0029 to T-0031. Changing a
  signature later means updating those scripts in the same PR.

## Log

- 2026-10-18 architect: created under T-0025. Enumerated current production includes (all std or in-component). Found the emitter string-literal and comment occurrences a naive grep would flag. Ready.
