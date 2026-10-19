---
id: T-0033
title: Add release.yml: source and binary release on v* tags
status: in-progress
area: cross-cutting
type: feature
priority: P3
depends_on: []
created: 2026-10-18
---

## User story

As someone who only wants the `asynctp` CLI and the `asyncrt` headers, I
want a tagged GitHub Release with a prebuilt Linux binary, so that I don't
have to build the transpiler from source.

## Problem

There is no release process: no tags, no published binaries. Consumers must
clone and build (`add_subdirectory` or the Makefile).

## Scope

- In: `.github/workflows/release.yml`, triggered by pushing a `v*` tag; a
  README "Releases" paragraph.
- Out: other platforms (macOS, Windows, arm64), packaging for distro
  package managers, a CMake package config / `install()` rules (CMake has
  none today), signing.
- Touches: `.github/workflows/release.yml`, `README.md`. No `transpiler/`,
  `runtime/`, `cmake/`, or `examples/` changes.

## Blueprint

1. `build` job in the `gcc:15` container (same toolchain as `ci.yml`, a
   read-only token): `verify.sh all`, then a Release build of `asynctp` and
   `asynctp-lsp` with `-static-libstdc++ -static-libgcc` (glibc stays
   dynamic, so the binaries need glibc >= 2.41, Debian trixie's).
2. Package `dotawait-<tag>-linux-x86_64.tar.gz` = `bin/asynctp`,
   `bin/asynctp-lsp` (stripped), `include/asyncrt/`, `LICENSE`, `README.md`,
   plus a `.sha256`. Smoke-test it: the packaged `asynctp` transpiles
   `examples/hello.async.cpp`, which then compiles against the packaged
   headers and runs.
3. `publish` job: the only job with `contents: write`. It runs no repository
   code: it downloads the artifact and runs `gh release create --verify-tag
   --generate-notes`. A tag containing `-` becomes a prerelease.

Invariants: no production code changes. Zero-dependency is kept because
the container and gh are release infrastructure only.

## Acceptance criteria

- [x] Packaging rehearsed locally (GCC 15.2): `ldd` shows only libc, and
      the smoke test prints `result = 12`.
- [x] actionlint clean.
- [ ] Pushing a throwaway prerelease tag (e.g. `v0.0.1-rc1`) produces a
      prerelease with both assets. Record the run URL, then delete the
      release and tag.
- [ ] The downloaded tarball's checksum matches its `.sha256`, and
      `bin/asynctp --help` runs on a stock Ubuntu 25.04+ or Debian trixie
      machine.

## Notes

- The workflow runs `verify.sh all` itself rather than trusting the tagged
  commit's CI status. This costs about a minute and needs no extra
  permission.
- Tags can only be pushed by people with write access. T-0032's rulesets
  can additionally restrict `v*` tag creation.

## Log

- 2026-10-18 architect: created as a placeholder under T-0025.
- 2026-10-19 claude (at the human's request): filled in and implemented `release.yml` and the README paragraph on branch `agent/T-0026` together with T-0026's `ci.yml`. Not yet run on GitHub.
