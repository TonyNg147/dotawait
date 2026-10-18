---
id: T-0030
title: Add ticket evidence markers and the ticket-consistency gate
status: ready
area: cross-cutting
type: feature
priority: P2
depends_on: [T-0028]
created: 2026-10-18
---

Epic: T-0025. Design doc: `docs/design/agent-pipeline-security.md`
threats T1, T4; canary C16.

This ticket touches `.github/` and `.claude/`, so it is a human or local
`/ship` ticket.

## User story

As the human reviewing an agent PR, I want CI to confirm four things:

- the PR implements the ticket its branch names;
- that ticket was actually ready, with its dependencies done;
- every acceptance criterion that names a test or fixture is reflected in
  the diff;
- the agent did not rewrite the ticket it is judged against.

Then I can spend my review on intent, not bookkeeping.

## Problem

- Acceptance criteria are free prose. A typical one from T-0001 reads:
  "Golden fixture `transpiler/tests/golden/braceless_return.async.cpp` +
  `braceless_return.expected.cpp`, registered with `run_case(asynctp_path,
  golden_dir, "braceless_return");` in `golden_tests.cpp` `main()`."
  Backticked tokens mix paths, code, and inputs. Extracting "the test this
  criterion names" heuristically would misfire, which violates the
  fail-loudly principle. **A small explicit convention is needed.**
- The agent edits its own ticket file in the PR. The developer runs
  `ticket.py status`, and the tester sets `done` on PASS
  (`.claude/agents/tester.md` "Verdict"). Today nothing stops the agent
  from also editing the acceptance criteria themselves.
- **Correction to the agreed design ("the referenced ticket is
  in-review").** `/ship` ends with the tester setting the ticket `done`
  inside the agent's worktree (`.claude/skills/ship/SKILL.md` step 4,
  tester.md "On PASS, set the ticket done"). An agent PR therefore arrives
  with the head status `done`, not `in-review`. The gate accepts
  `in-review` or `done` on head (decision D-E in T-0025).
- `ticket.py status` also regenerates the whole `docs/tickets/README.md`
  index (`write_index()`), with rows sorted by status. A status change
  therefore moves that ticket's row, and the index diff must be checked by
  ID, not by line position.

## Scope

- In:
  - the `[evidence: …]` marker convention, documented in
    `docs/tickets/README.md` and the ticket template;
  - `tools/ci/ticket_gate.py` and its tests;
  - the `agent ticket consistency` job in `agent-gates.yml`.
- Out:
  - Retrofitting markers into existing tickets. The architect does this
    per ticket when queuing it for the agent (decision D-F).
  - Path-level scope rules (T-0029).
  - Whether tests fail on base (T-0031).
- Touches: `tools/ci/ticket_gate.py`, `tools/ci/tests/test_ticket_gate.py`,
  `.github/workflows/agent-gates.yml`, `docs/tickets/README.md`,
  `.claude/skills/ticket/templates/ticket.md`, `tools/ci/README.md`.

## Blueprint

### 1. Convention (document in `docs/tickets/README.md`, section "Machine-checkable evidence")

- An acceptance-criterion item is a line starting with `- [ ]` or `- [x]`
  at column 0, plus its indented continuation lines up to the next such
  line or the end of the section.
- An item may contain one or more `[evidence: e1, e2, …]` markers. Each
  entry is one of:
  - `path`: the path is **added, modified, or the target of a rename** in
    the PR diff;
  - `path::needle`: as above, **and** `needle` occurs in the `+` lines of
    that file's diff (e.g.
    `transpiler/tests/lowering_tests.cpp::test_braceless_if`).
    Needles may not contain `]` or `,`.
- Items without a marker are "manual". The gate lists them for the
  reviewer.
- Tickets of type `bug`, `feature`, or `limitation` must have at least
  one marker. `chore` may have none.
- `[allow-change: <glob>]` (T-0029) is documented in the same section.
- The architect writes markers on `master`. The gate reads them from the
  **merge-base** copy only.

Template (`.claude/skills/ticket/templates/ticket.md`): change the
example criteria to show markers, e.g.
`- [ ] New/updated unit tests in \`<suite>\` covering: … [evidence: transpiler/tests/<suite>.cpp::test_<name>]`.

### 2. `tools/ci/ticket_gate.py`

CLI: `ticket_gate.py --repo <head checkout> --base <merge-base> --head <sha> --branch <head_ref>`.
It uses `cilib`. Rules, all reported, exit 1 on any:

| Rule | Check |
|---|---|
| K1 | `ticket_id_from_branch` succeeds; the base has exactly one `docs/tickets/T-NNNN-*.md` |
| K2 | Base status ∈ {`ready`, `in-progress`}. Every `depends_on` ID is `done` **on base** (read each dependency's file from base). |
| K3 | Head status ∈ {`in-review`, `done`} |
| K4 | **Own-ticket integrity.** Diff base vs head ticket text with `difflib.SequenceMatcher` over lines. Allowed: <br>(a) the front-matter `status:` line changes value; <br>(b) a line in `## Acceptance criteria` changes only `- [ ]` → `- [x]`; <br>(c) inserted lines at the end of `## Log` (after its last existing line), each matching `^- \d{4}-\d{2}-\d{2} [a-z-]+: \S`. <br>Every other replace/insert/delete is a violation and is reported with its line number. |
| K5 | **Index integrity.** `docs/tickets/README.md` (if changed). Text outside the `<!-- BEGIN TICKET INDEX -->`/`END` markers must be identical. Inside, compare rows **as a set keyed by ID**, not by position: `write_index()` sorts rows by status, so a status change moves the row. Every other row is byte-identical and the relative order of the others is unchanged. The own row differs only in the Status cell, which must equal the head ticket's status. |
| K6 | **Evidence.** For each base AC item, check each marker entry against `changed_files` / `added_lines`. Report missing entries as `K6: AC #3 ("Golden fixture …"): transpiler/tests/golden/x.expected.cpp not in diff` plus the list of changed paths (first 30). |
| K7 | Type ∈ {bug, feature, limitation} and zero markers in the base ticket → `K7: T-NNNN has no [evidence: …] markers; the architect must add them on master before an agent run` |

Step summary: a table with one row per AC: `#`, first 80 chars,
evidence entries, ✓/✗/manual.

### 3. Job in `.github/workflows/agent-gates.yml`

Add next to `scope` (T-0029). If this ticket lands first, create the file
with T-0029's header, which is identical. The job is the same as `scope`
except:
- `name: agent ticket consistency`;
- no cmake in the install step (`python3` only);
- last step:
  `python3 "$RUNNER_TEMP/base/tools/ci/ticket_gate.py" --repo . --base "$MB" --head "$HEAD_SHA" --branch "$HEAD_REF"`;
- the base-tools test checks `ticket_gate.py`.

### Invariants

No product code. The fail-loudly principle is applied to the process:
there is no heuristic parsing of prose, only explicit markers, and
missing markers on a behaviour ticket are an error, not a pass.

## Acceptance criteria

- [ ] `tools/ci/tests/test_ticket_gate.py`: one temp-repo case per rule,
      asserting the exact rule ID. [evidence: tools/ci/tests/test_ticket_gate.py]
  - K1: branch `agent/T-0099`, no such ticket on base → K1.
  - K2: base status `proposed` → K2. A dependency `ready` on base → K2
    naming it.
  - K3: head status `in-progress` → K3. `in-review` and `done` both pass.
  - K4 cases:
    - an AC text edit → K4 with its line;
    - a checkbox tick only → OK;
    - a Log line appended → OK;
    - a Log line edited in the middle → K4;
    - an `[evidence: …]` marker deleted in head → K4. The marker is
      still enforced from base.
  - K5: another ticket's row changed → K5. Own row with a status
    mismatching the head ticket → K5. Own row moved to a new position by
    the status sort (output of a real `ticket.py status` run) → OK.
  - K6 cases:
    - path missing → K6;
    - path present → OK;
    - `path::needle` with the needle only in a *removed* line → K6;
    - needle in an added line → OK;
    - rename target counts as present.
  - K7: `bug` ticket with no markers → K7. `chore` with none → OK.
- [ ] Running against a copy of T-0027 (which carries markers) with a
      synthetic diff that touches its evidence paths passes. The same
      diff without `cmake/check_golden_compile.cmake` fails K6 for that
      AC. [evidence: tools/ci/ticket_gate.py]
- [ ] `agent ticket consistency` job present; skipped on human PRs; canary
      C16 (design doc §7) is red with K4. Record the run URL in the Log.
      [evidence: .github/workflows/agent-gates.yml]
- [ ] Added to the required checks of `master`.
- [ ] `docs/tickets/README.md` "Machine-checkable evidence" section and the
      template examples updated. [evidence: .claude/skills/ticket/templates/ticket.md]
- [ ] `verify.sh all` passes; `tools selftest` green.

## Notes

- **D-E (human):** head status `in-review` or `done`? Recommended: accept
  both. `done` on an unmerged branch means "tester PASS". Only the merge
  makes it true on master. The alternative is changing the tester agent
  to stop at `in-review` for agent runs, which forks the `/ship` flow.
- **D-F (human):** retrofit markers into T-0001 to T-0024 now, or lazily?
  Recommended: lazily. The architect adds markers when a ticket is queued
  for the agent (stage 2), since T-0017 will rename paths that many of
  those criteria name anyway. T-0026 to T-0033 already carry markers.
- After T-0017, markers naming `*.async.cpp` paths become stale. K6
  fails loudly with the missing path, and the architect updates the
  ticket. This is intended.

## Log

- 2026-10-18 architect: created under T-0025. Confirmed from ship/tester definitions that agent PRs arrive with status `done`, not `in-review` (design correction, D-E). Confirmed ticket.py regenerates the whole index on every status change, sorted by status, so rows move (K5 compares by ID). Ready.
