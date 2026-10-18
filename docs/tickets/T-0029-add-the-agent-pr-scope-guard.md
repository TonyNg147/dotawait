---
id: T-0029
title: Add the agent-PR scope guard
status: ready
area: cross-cutting
type: feature
priority: P2
depends_on: [T-0028]
created: 2026-10-18
---

Epic: T-0025. Design doc: `docs/design/agent-pipeline-security.md`
threats T4, T5, T6, T17; canaries C12 to C14.

This ticket touches `.github/`, so it is a human or local `/ship`
ticket, never an agent-tracker run.

## User story

As the human who merges agent PRs, I want a required check that fails
when an `agent/T-NNNN` PR touches the pipeline's own controls, or removes
or weakens existing tests, so that "CI is green" keeps meaning "the
change works" and not "the agent edited the tests".

## Problem

Today nothing distinguishes an agent PR from a human one. Concretely:

- `ci.yml` (T-0026) runs `.claude/skills/verify/scripts/verify.sh` **from
  the PR head**. An agent PR that changes `verify.sh` to `exit 0`, or drops
  a suite from `transpiler/CMakeLists.txt`'s `foreach(test_name …)`, gets a
  green `verify (full)`.
- `runtime/tests/test_support.hpp` and `transpiler/tests/test_support.hpp`
  define `CHECK`/`CHECK_EQ`. Redefining them as no-ops passes every suite.
- Golden expectations (`transpiler/tests/golden/*.expected.cpp`) can be
  regenerated to match wrong output. The developer agent is told not to
  do this (`.claude/agents/developer.md`: "Never weaken or delete an
  existing test"), but that is an instruction, not a control.
- The ctest name set is observable without building:
  `cmake -S . -B cfg && ctest --test-dir cfg --show-only=json-v1` takes
  0.7 s and lists `lexer_tests, parser_tests, lowering_tests,
  golden_tests, task_tests, executor_tests, hello_example,
  yielding_example, consumer_example` on commit `2a7c317`.

## Scope

- In:
  - `tools/ci/scope_guard.py` and its tests;
  - the `[allow-change: <glob>]` ticket marker (read from the **base**
    ticket);
  - `.github/workflows/agent-gates.yml` created with the shared header
    and the `agent scope guard` job.
- Out:
  - Ticket-file edit rules and evidence markers (T-0030).
  - Whether new tests fail on base (T-0031).
  - Path restrictions enforced by GitHub itself (T-0032).
- Touches: `tools/ci/scope_guard.py`, `tools/ci/tests/test_scope_guard.py`,
  `.github/workflows/agent-gates.yml` (new), `tools/ci/README.md`,
  `docs/tickets/README.md` (marker documentation).

## Blueprint

### 1. `tools/ci/scope_guard.py`

CLI:
`scope_guard.py --repo <head checkout> --base <merge-base sha> --head <sha> --branch <head_ref> [--no-configure]`.
It uses `cilib` (T-0028) for git, ticket parsing, globs, annotations, and
the step summary.

- Ticket ID: `cilib.ticket_id_from_branch(branch)`. Returns `None` → exit 1
  with `branch must be agent/T-NNNN`.
- Allow markers: read the ticket from `--base` (`find_ticket_path` +
  `show`). Collect every `[allow-change: <glob>]` in the whole file
  (regex `\[allow-change:\s*([^\]]+)\]`, comma-separated globs).
- The head copy of the ticket is ignored for markers, so a PR cannot
  grant itself permission.

The rules run over `cilib.changed_files(repo, base, head)` and
`numstat`. Every violation is reported. Exit 1 if there is any
violation.

| Rule | Fails when | Can an allow marker override it? |
|---|---|---|
| **S1 forbidden paths** | Any change (any status) to a path matching `.github/**`, `.claude/**`, `CLAUDE.md`, `.gitignore`, `.gitattributes`, `**/CODEOWNERS`, `tools/**`, `docs/design/**`, `docs/sketches/**`, `LICENSE` | **No.** These need a human PR or a conscious admin bypass. |
| **S2 other tickets** | Any change under `docs/` except the ticket's own `docs/tickets/T-NNNN-*.md` and `docs/tickets/README.md`. T-0030 checks *how* those two changed. | No |
| **S3a test removal** | Status `D` or `R` (rename = old path removed) for a path under `transpiler/tests/**`, `runtime/tests/**`, `examples/**` | Yes, when the *old* path matches a marker |
| **S3b golden edit** | Status `M` on an existing `transpiler/tests/golden/**` file | Yes |
| **S3c harness edit** | Any change to `**/tests/test_support.hpp` | Yes |
| **S3d check count** | For each `M` file under `**/tests/**` with a `.cpp`/`.hpp` suffix: the count of `\bCHECK(_EQ)?\s*\(` or of `^\s*void\s+test_\w+\s*\(` in head is lower than in base. For `golden_tests.cpp`, also the count of `run_case\s*\(`. | Yes |
| **S3e ctest set** | Unless `--no-configure`: configure base and head into temp dirs (`cmake -S <tree> -B <tmp> -DCMAKE_BUILD_TYPE=Debug`) and list names with `ctest --show-only=json-v1`. Fails if a base test name is missing in head. A failed head configure is also a violation; a failed base configure is a tool error (exit 2). | Yes, via the marker `[allow-change: ctest:<name>]` |
| **S3f Makefile test recipe** | The number of recipe lines (tab-indented) under the `test:` target in `Makefile` decreases | Yes, via `[allow-change: Makefile]` |
| **S4 binaries / special files** | `numstat` says binary, or `git diff --summary` shows a new mode `120000` (symlink) or `160000` (submodule) | No |
| **S5 size** | More than 60 files changed, or more than 3000 added+deleted lines. A one-ticket PR should be smaller. | Yes, `[allow-change: size]` |

Output:
- one line per violation:
  `scope_guard: S3b: transpiler/tests/golden/simple_fn.expected.cpp: existing golden expectation changed (add [allow-change: transpiler/tests/golden/simple_fn.expected.cpp] to T-NNNN on master if intended)`;
- a step-summary table (rule, path, hint);
- on success: `scope_guard: T-NNNN: <n> files checked, OK (allow markers: …)`.

Exit codes: 0 OK, 1 violations, 2 tool error (bad args, git failure,
base configure failure). In CI, exit 2 is red too: fail loudly.

### 2. `.github/workflows/agent-gates.yml` (created here; T-0030 and T-0031 add jobs)

```yaml
# Gates that apply only to agent-written PRs (branch agent/T-NNNN).
# Each job is skipped for other PRs; a skipped job reports Success, so these
# can be required checks without blocking human PRs. The classification is
# trustworthy because the agent's GitHub App can only create agent/** (T-0032).
# Gate scripts run from the MERGE-BASE copy of tools/ci, so a PR cannot
# change the rules it is judged by.
name: agent-gates
on:
  pull_request:
permissions:
  contents: read
concurrency:
  group: agent-gates-${{ github.event.pull_request.number }}
  cancel-in-progress: true
env:
  HEAD_REF: ${{ github.head_ref }}                       # untrusted: use only as "$HEAD_REF"
  BASE_SHA: ${{ github.event.pull_request.base.sha }}
  HEAD_SHA: ${{ github.event.pull_request.head.sha }}
  DEBIAN_FRONTEND: noninteractive
jobs:
  scope:
    name: agent scope guard
    if: startsWith(github.head_ref, 'agent/')
    runs-on: ubuntu-24.04
    timeout-minutes: 15
    container: { image: 'gcc:15' }
    steps:
      - name: Install tools
        run: apt-get update && apt-get install -y --no-install-recommends cmake python3
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          ref: ${{ github.event.pull_request.head.sha }}
          fetch-depth: 0
          persist-credentials: false
      - name: Prepare base worktree (gate code comes from here)
        run: |
          git config --global --add safe.directory "$GITHUB_WORKSPACE"
          MB="$(git merge-base "$BASE_SHA" "$HEAD_SHA")"
          echo "MB=$MB" >> "$GITHUB_ENV"
          git worktree add --detach "$RUNNER_TEMP/base" "$MB"
          test -f "$RUNNER_TEMP/base/tools/ci/scope_guard.py" \
            || { echo "::error::gate tools absent on base $MB"; exit 2; }
      - name: Scope guard
        run: python3 "$RUNNER_TEMP/base/tools/ci/scope_guard.py" --repo . --base "$MB" --head "$HEAD_SHA" --branch "$HEAD_REF"
```

Note: until this ticket is on `master`, the base has no `scope_guard.py`,
so the job fails with "gate tools absent on base". That is correct and
fails loudly. Agent PRs must not run before stage 2 anyway.

### 3. Docs

- `docs/tickets/README.md`: document `[allow-change: <glob>]`. It is
  written by the architect on master, read from the base only, and never
  honoured for S1/S2/S4.
- `tools/ci/README.md`: the rule table.

### Invariants

None of `transpiler/`, `runtime/`, or the build is touched. The guard
enforces CLAUDE.md's "Every behavior change ships with a test" and the
developer rule "never weaken or delete an existing test" as controls
rather than instructions.

## Acceptance criteria

- [ ] `tools/ci/tests/test_scope_guard.py` builds temp git repos (base
      commit + head commit) and asserts the exact rule IDs reported. Each
      case fails if its rule is removed.
      [evidence: tools/ci/tests/test_scope_guard.py]
  - S1: edit to `.claude/skills/verify/scripts/verify.sh` → S1, even with
    `[allow-change: .claude/**]` in the base ticket. Same for
    `.github/workflows/ci.yml`, `CLAUDE.md`, `tools/ci/cilib.py`, and
    `src/CODEOWNERS`.
  - S2: edit to `docs/tickets/T-0001-….md` on branch `agent/T-0002` → S2.
    Edits to its own ticket and to `docs/tickets/README.md` → no S2.
  - S3a: delete `transpiler/tests/lexer_tests.cpp` → S3a. Rename
    `examples/hello.async.cpp` → `examples/hello.cpp` → S3a, and none with
    `[allow-change: examples/**]` in the **base** ticket. The same marker
    added only in the **head** ticket → still S3a.
  - S3b: modify `golden/simple_fn.expected.cpp` → S3b. Adding a *new*
    golden pair → no violation.
  - S3c: modify `transpiler/tests/test_support.hpp` → S3c.
  - S3d: remove one `CHECK_EQ(` from a suite → S3d. Replace a `CHECK_EQ`
    with a `CHECK` → no violation (the count is unchanged). Remove a
    `run_case(` from `golden_tests.cpp` → S3d.
  - S3e: covered with a minimal CMake project in the temp repo
    (`add_test(NAME a …)`, `add_test(NAME b …)`; head removes `b`) → S3e.
    Skipped automatically with a printed notice if `cmake` is absent.
  - S3f: delete one recipe line under `test:` in a temp Makefile → S3f.
  - S4: add a binary file (`\x00` bytes) → S4. Add a symlink → S4.
  - S5: 61 changed files → S5. With `[allow-change: size]` → none.
  - Branch `feature/x` → exit 1 `branch must be agent/T-NNNN`.
- [ ] On the real repo, `python3 tools/ci/scope_guard.py --repo . --base HEAD~1 --head HEAD --branch agent/T-0029`
      against a commit that only adds a new test file reports OK
      (record the output in the Log). [evidence: tools/ci/scope_guard.py]
- [ ] `.github/workflows/agent-gates.yml` matches the Blueprint.
      [evidence: .github/workflows/agent-gates.yml]
  - On a human PR, the `agent scope guard` check shows "Skipped" and the
    PR stays mergeable.
  - Canaries C12 to C14 (design doc §7), pushed with the App token once
    T-0032 is done, or with the human's token before that:
    `agent scope guard` red with S1, S3d, and S3b respectively. Record the
    run URLs in the Log.
- [ ] `agent scope guard` added to the required checks of `master`.
- [ ] `[allow-change: …]` documented in `docs/tickets/README.md`; rule
      table in `tools/ci/README.md`.
- [ ] `verify.sh all` passes; `tools selftest` green.

## Notes

- **T-0017 cannot pass S1** because it edits `CLAUDE.md` and `.claude/`.
  It also renames every golden input and example (S3a). Run T-0017
  locally (not via the tracker), or merge with a conscious admin bypass.
  Its ticket also needs `[allow-change: transpiler/tests/golden/**]` and
  `[allow-change: examples/**]` if it ever goes through the gate. Logged
  on T-0017.
- **T-0012** may regenerate golden expectations (its criteria say "only
  where per-function `#line` removal changes them"). If it does, the
  architect adds `[allow-change: transpiler/tests/golden/<case>.expected.cpp]`
  before queuing it for the agent.
- Thresholds in S5 are first guesses. Tune them from the stage-2 metrics.
- Why the base ticket for markers: the agent writes the head copy. Markers
  must come from a human-merged text (design doc P1).

## Log

- 2026-10-18 architect: created under T-0025. Verified that the ctest name set is available from configure alone (0.7 s, 9 names). Identified verify.sh / test_support.hpp / CMake test lists as weakening vectors. Ready.
