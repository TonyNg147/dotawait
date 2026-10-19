---
id: T-0026
title: Add the ci.yml build and verify workflow
status: in-progress
area: cross-cutting
type: chore
priority: P1
depends_on: []
created: 2026-10-18
---

Epic: T-0025. Design doc: `docs/design/agent-pipeline-security.md`.
Sketch: `docs/sketches/ci-agent-pipeline.html`. **The human implements this
ticket.** The workflow below is meant to be copied as-is.

## User story

As a maintainer of dotawait, I want every PR and every push to `master`
built and tested by GitHub Actions with the same commands I run locally,
so that a red tree cannot be merged. This applies to agent-written PRs
from `agent/T-NNNN` as well.

## Problem

There is no CI. Observed on 2026-10-18:

- `ls .github` fails because the directory does not exist. The repo is
  public, user-owned (`TonyNg147/dotawait`), and the default branch is
  `master` (GitHub API: `private: false`, `owner.type: User`).
- **`verify.sh` is not in git.** `git ls-files` on `master` (commit
  `2a7c317`) lists no `.claude/`, `CLAUDE.md`, or `docs/` files. `git status`
  shows them untracked (`?? .claude/`, `?? CLAUDE.md`, `?? docs/`), and
  `README.md`/`.gitignore` are modified but not committed. A
  `git archive HEAD` export has no `.claude/` directory. A workflow that
  calls `.claude/skills/verify/scripts/verify.sh` fails on a fresh
  checkout until these files are committed (step 0).
- Hosted `ubuntu-24.04` runners ship GCC 14. The project targets GCC 15
  (CLAUDE.md "Toolchain").
- `verify.sh` runs non-interactively from a clean export. I ran
  `git archive HEAD` plus a copy of `.claude/` with `</dev/null` on 8
  cores, GCC 15.2, CMake 4.3.4:

  | mode | exit | wall time | what it runs |
  |---|---|---|---|
  | `quick` | 0 | 4 s | configure + build `build/` + ctest minus `consumer_example` (8 tests) |
  | `full` | 0 | 16 s | `quick`'s cmake_suite **with** `consumer_example` (9/9 tests, consumer 4.1 s) + `make BUILD=build-make test` (Makefile parity) |
  | `asan` | 0 | 4 s | ASan+UBSan build in `build-asan/` + ctest minus `consumer_example` |

  No compiler warnings appeared in any log (`grep -ci warning` = 0).

### What the `gcc:15` image contains

Docker could not run locally: `docker` resolves to Docker Desktop, but
"The command 'docker' could not be found in this WSL 2 distro". No image
was pulled. The facts below come from the official image sources
(`docker-library/gcc` `15/Dockerfile` and `library/gcc` manifest,
fetched 2026-10-18):

| Need | In `gcc:15`? | Evidence | Action |
|---|---|---|---|
| GCC 15 | yes, **15.3.0** (`Tags: 15.3.0, 15.3, 15, … 15-trixie`) | built from source into `/usr/local`; Debian's `/usr/bin/g++` is `dpkg-divert`ed to `g++.orig` | set `CXX=g++` and assert `g++ -dumpversion` starts with `15.` |
| CMake ≥ 3.20 | **no** | base `buildpack-deps:trixie` has no cmake | `apt-get install cmake` (Debian trixie ships 3.31.x) |
| make | yes | `buildpack-deps:trixie` installs `make` | none |
| git | yes | `buildpack-deps:trixie-scm` installs `git` | none. `actions/checkout` does a real clone rather than a tarball, which the gate tickets need. |
| python3 | yes, but only transitively (via `mercurial` in `-scm`) | not needed by this workflow | the gate tickets install it explicitly |
| `nproc` | yes (coreutils) | used by `verify.sh` | none |

The local toolchain is 15.2, CI uses 15.3. That is acceptable: both are
GCC 15. Pin the image digest only if a minor bump ever breaks the build
(see Notes).

## Scope

- In: `.github/workflows/ci.yml` (two jobs via a matrix: `verify (full)`
  and `verify (asan)`); committing the agent tooling so CI can call
  `verify.sh`; a ruleset that makes both checks required on `master`; a
  README "Continuous integration" paragraph.
- Out:
  - Agent-only gates (T-0029, T-0030, T-0031).
  - The include check (T-0028).
  - Golden compile (T-0027, which runs inside `verify.sh`, so this
    workflow picks it up automatically).
  - Release (T-0033).
  - Review and merge rules plus the agent identity (T-0032).
  - Caching (measured as not worth it, see Notes).
  - clangd in CI (T-0021's real-clangd tests skip with exit 77).
- Touches: `.github/workflows/ci.yml` (new), `README.md`, GitHub settings.
  No `transpiler/`, `runtime/`, `cmake/`, or `examples/` changes.

### Why two jobs, not four (deviation from the agreed design)

The agreed design listed four jobs (build-test, parity, asan, consumer).
`verify.sh` has only `quick | full | asan | all` modes, and **`full` already
is build-test + consumer + parity** in one 16 s run. Four jobs would need
one of two things:

- (a) raw cmake/make commands in YAML. These drift from `verify.sh`, which
  is exactly what T-0015 and T-0017 are about to change: launcher, renames,
  new ctests.
- (b) new `verify.sh` modes. That edits `.claude/`, which is CODEOWNERS
  territory, and buys nothing at this size: each extra job pays a container
  pull plus an apt install (~1 min) to save ~10 s of build.

Calling only `verify.sh` keeps CI valid across T-0015/T-0017 with zero
edits, and CI cannot pass while `verify.sh full` fails locally. If triage
of a red `full` ever gets painful, split it then with option (b). This is
recorded as decision D-B in T-0025.

## Blueprint

### Step 0: prerequisite (human, git)

Commit the agent tooling and docs to `master`: `.claude/` (except
`settings.local.json`, already ignored), `CLAUDE.md`, `docs/`, and the
pending `README.md`/`.gitignore` edits. Check the result:

```sh
git ls-files -s .claude/skills/verify/scripts/verify.sh   # must print mode 100755
```

The working-tree file is `-rwxr-xr-x` and `core.fileMode=true`, so a
plain `git add` records 100755. If it shows 100644, run
`git update-index --chmod=+x .claude/skills/verify/scripts/verify.sh`.

### Step 1: create `.github/workflows/ci.yml` with exactly this content

Action SHAs were resolved through the GitHub API on 2026-10-18
(`actions/checkout` v7.0.1 → `3d3c42e5…`, `actions/upload-artifact` v7.0.2
→ `cf430e03…`). Keep the SHA pin with the version comment. Dependabot
(`github-actions` ecosystem) can bump them later.

```yaml
# Build + test on every PR and every push to master.
# Calls verify.sh only, so CI runs exactly what developers and agents run
# locally; build changes (T-0015 launcher, T-0017 renames) need no edit here.
name: ci

on:
  pull_request:
  push:
    branches: [master]
  workflow_dispatch:

# Read-only token, no secrets: this workflow builds and runs PR code,
# including agent-written PRs.
permissions:
  contents: read

concurrency:
  group: ci-${{ github.event.pull_request.number || github.ref }}
  cancel-in-progress: ${{ github.event_name == 'pull_request' }}

jobs:
  verify:
    # The check names "verify (full)" and "verify (asan)" are what the
    # master ruleset requires. Renaming them breaks branch protection.
    name: verify (${{ matrix.mode }})
    runs-on: ubuntu-24.04
    timeout-minutes: 20
    container:
      image: gcc:15          # GCC 15.x on Debian trixie; runners only ship GCC 14
    strategy:
      fail-fast: false
      matrix:
        mode: [full, asan]   # full = cmake build+ctest (incl. consumer) + Makefile parity
    env:
      CC: gcc
      CXX: g++
      DEBIAN_FRONTEND: noninteractive
    steps:
      - name: Install CMake (not in the gcc image)
        run: |
          apt-get update
          apt-get install -y --no-install-recommends cmake

      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          persist-credentials: false   # PR code runs next; keep the token out of .git/config

      - name: Toolchain
        run: |
          g++ --version | head -n1
          cmake --version | head -n1
          make --version | head -n1
          case "$(g++ -dumpversion)" in
            15*) ;;
            *) echo "::error::expected GCC 15, got $(g++ -dumpversion)"; exit 1 ;;
          esac

      - name: verify.sh ${{ matrix.mode }}
        run: .claude/skills/verify/scripts/verify.sh ${{ matrix.mode }}

      - name: Upload test logs on failure
        if: failure()
        uses: actions/upload-artifact@cf430e030ddbb5b0abf93d22962f4752f3646cd9 # v7.0.2
        with:
          name: test-logs-${{ matrix.mode }}
          path: |
            build/Testing/Temporary/LastTest*.log
            build-asan/Testing/Temporary/LastTest*.log
          if-no-files-found: ignore
          retention-days: 7
```

Notes on choices, so you can change them knowingly:

- `pull_request` without a branch filter covers PRs into any base. Agent PRs
  target `master`.
- **Do not add a `paths:` filter.** A workflow that is skipped by a path
  filter leaves required checks "Expected" forever and blocks the merge.
- Do not use `pull_request_target`. It runs with a write token against PR
  code. See the design doc, threat T7.
- The container runs as root, so `apt-get` needs no `sudo`.
  `actions/checkout` marks the workspace as a git `safe.directory` itself.
  This workflow runs no other git commands.

### Step 2: required checks (human, GitHub settings)

Push the workflow on a branch, open a PR, and let it run once. GitHub only
offers check names that have reported in the last 7 days. Then go to
**Settings → Rules → Rulesets → New ruleset → New branch ruleset**:

| Field | Value |
|---|---|
| Ruleset name | `master: required CI` |
| Enforcement status | Active |
| Bypass list | Repository admin, mode **Always**. This keeps your current direct-push workflow; T-0032 tightens it. |
| Target branches | Include default branch |
| Rules | ☑ Restrict deletions · ☑ Block force pushes · ☑ Require status checks to pass: add `verify (full)` and `verify (asan)`, source **GitHub Actions**; ☐ "Require branches to be up to date" (leave off for now; it forces a rebase per merge) |

T-0032 adds the PR/review ruleset and the agent-branch confinement on top.
Rulesets stack, so this one stays as is.

### Step 3: README

In `README.md` under "Building this repo", add a short **Continuous
integration** paragraph:

- `.github/workflows/ci.yml` runs `verify.sh full` and `verify.sh asan` in
  the `gcc:15` container on every PR and every push to `master`;
- both checks are required on `master`;
- to reproduce a red check locally, run the same `verify.sh` mode.

### Invariants

- **Makefile/CMake parity.** This is enforced in CI for the first time,
  because `full` runs `make BUILD=build-make test`.
- **Zero-dependency.** Untouched: the container and apt packages are CI
  infrastructure, not production code. CMake is already a build
  prerequisite.
- **transpiler/runtime independence.** Untouched; CI only runs existing
  targets.
- **Fail loudly.** The Toolchain step fails with a named error when the
  image's GCC is not 15. `fail-fast: false` makes ASan report even when
  `full` fails.

## Acceptance criteria

- [ ] Step 0: on `master`, `git ls-files -s .claude/skills/verify/scripts/verify.sh`
      prints mode `100755`, and `git ls-files CLAUDE.md docs/tickets/README.md`
      prints both paths.
- [ ] `.github/workflows/ci.yml` matches the Blueprint (SHA bumps allowed).
      If `actionlint` is available, it reports nothing. It is not installed
      on the architect's machine; `docker run rhysd/actionlint` is one
      option.
- [ ] On a no-op PR, both `verify (full)` and `verify (asan)` are green.
      The Toolchain log shows `g++ (GCC) 15.` and `cmake version 3.` with
      minor ≥ 20. The `full` log shows `100% tests passed … out of 9` and
      the `==> Makefile parity (build-make/)` step.
- [ ] Negative proofs. Use throwaway PRs, close them unmerged, and record
      the run URLs in this ticket's Log:
  - (a) Change one expected string in a `CHECK_EQ` in
    `transpiler/tests/lexer_tests.cpp`: both checks red.
  - (b) Delete `transpiler/src/emitter.cpp` from `TRANSPILER_SRCS` in the
    `Makefile`: `verify (full)` red in the Makefile parity step, while its
    ctest part passed. This proves parity is checked.
  - (c) Add `new int(1);` (leaked) to a test in `runtime/tests/task_tests.cpp`:
    `verify (asan)` red with `LeakSanitizer: detected memory leaks`. This
    proves LSan works inside the container. If instead the log says
    `LeakSanitizer has encountered a fatal error` or `does not work under
    ptrace`, add `options: --cap-add=SYS_PTRACE` under `container:` for
    the asan leg and note it in the Log.
- [ ] A push to `master` triggers `ci` and it is green.
- [ ] Ruleset `master: required CI` is active with both checks required.
      Proof: proof (a)'s PR shows "Merging is blocked" for a non-bypass
      view. Optionally check from a second account, or rely on the
      ruleset "Insights" page.
- [ ] Per-job wall time (container + apt + verify) recorded in the Log.
      Expected 1–3 min. If either job exceeds 5 min, open a follow-up for
      a prebuilt CI image (Notes).
- [ ] README "Continuous integration" paragraph added (Step 3).
- [ ] `verify.sh all` passes locally on the merged tree. No source changes
      are expected, but this guards the README edit and the committed
      tooling.

## Notes

- **Caching: none, by measurement.** The whole `full` run is 16 s and
  `asan` 4 s on 8 cores; hosted runners have 4 vCPUs, so expect ~2× that.
  The cost is the container pull plus `apt-get` (~30–60 s). ccache or
  `actions/cache` would save seconds and adds a cache-poisoning surface
  for agent PRs (design doc, threat T7). If job time grows, the better
  lever is a prebuilt image (`gcc:15` + cmake + python3) in GHCR, built by
  a separate owner-only workflow.
- **Image pinning.** `gcc:15` floats within GCC 15 (15.3.0 today). That is
  fine for a C++20 project that wants the current GCC 15. If a point
  release ever breaks the build, pin `gcc:15@sha256:<digest>`.
- **ASan on hosted runners.** If ASan aborts at startup with
  `AddressSanitizer:DEADLYSIGNAL` loops, the host's `vm.mmap_rnd_bits` is
  too high for the sanitizer runtime. GCC 15's libsanitizer should handle
  it, and a container job cannot sysctl the host. The fallback is running
  the asan leg on the host runner with a GCC 15 toolchain.
- **Interplay with T-0009 children.** T-0015 adds ctests
  (`compile_commands_check`, `async_consumer_check`, `auto_mode`) and
  T-0017 renames fixtures. Both are reached through `verify.sh full`, so
  this workflow needs no change. T-0021 to T-0024 real-clangd tests skip
  with exit 77, because there is no clangd in `gcc:15`. Installing clangd
  in CI is a later decision.
- **Agent PRs.** This workflow runs on `agent/*` PRs with the same
  read-only token. Whether an agent PR can alter what CI runs (`verify.sh`,
  CMake test lists) is handled by T-0029 (scope guard) and T-0032
  (CODEOWNERS, no `workflows` permission for the agent's token).

## Log

- 2026-10-18 architect: created under T-0025. Reproduced verify.sh quick/full/asan non-interactively on a clean `git archive` export (+.claude) with times. Found that `.claude/`, `CLAUDE.md`, and `docs/` are untracked (step 0). gcc:15 contents come from the docker-library sources because Docker is not usable in this WSL distro. Deviated from the 4-job design to 2 matrix jobs that call verify.sh (rationale in Scope). Ready.
- 2026-10-19 claude (at the human's request): step 0 already holds on `agents/base` (`verify.sh` tracked as 100755). Step 1: `.github/workflows/ci.yml` added verbatim from the blueprint; both action SHAs re-checked against the `v7.0.1`/`v7.0.2` tags with `git ls-remote`; actionlint 1.7.7 clean (shellcheck not installed). Step 3: README "Continuous integration" paragraph added. Still open: first runs on GitHub, negative proofs (a)-(c), the push-to-master run (needs `agents/base` merged into `master`), the ruleset (step 2), job wall times.
