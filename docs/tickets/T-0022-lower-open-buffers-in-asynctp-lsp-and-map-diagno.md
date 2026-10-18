---
id: T-0022
title: Lower open buffers in asynctp-lsp and map diagnostics
status: proposed
area: transpiler
type: feature
priority: P1
depends_on: [T-0021, T-0020, T-0012, T-0018]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0021 (proxy), T-0020 (tolerant mode), T-0012 (source map), T-0018 (Q2 encoding, Q5 sync cost).

## User story

As a C++ developer, I want clangd's squiggles in my async file to sit on
the code I wrote, and asynctp's own errors (for example "'.await' binds
tighter than 'co_yield'") to appear as editor diagnostics while I type, so
that I never see `unknown type name 'async'`.

## Problem

After T-0021, clangd receives the raw buffer and reports `async` and
`.await` as errors. The proxy must give clangd lowered text and translate
clangd's answers back.

## Scope

- In:
  - Intercept `textDocument/didOpen`, `didChange`, `didSave` and `didClose` for C/C++ documents.
  - Keep the editor's original text per URI: apply incremental changes ourselves, using the negotiated position encoding.
  - Call `lower_file(..., Mode::Tolerant)`.
  - Send clangd `didOpen` with the lowered text, and `didChange` as full-text replacement with the same version number.
  - Keep `(uri, version) -> SourceMap`.
  - Rewrite `textDocument/publishDiagnostics` ranges, including `relatedInformation` in the same document, back to the original.
  - Merge asynctp's tolerant-mode diagnostics as `source: "asynctp"`, severity Error.
  - **Documents without markers:** pass the text through unchanged with an identity map (zero cost).
- Out: request/response mapping for completion, navigation and the rest (T-0023); headers (T-0024).

## Design outline

- **Encoding:** convert between LSP positions and byte offsets per line using the negotiated `positionEncoding` (spike Q2). Use UTF-16 conversion if the client does not offer utf-8.
- **Diagnostics in generated-only segments** (for example inside an inserted `co_await (`): map to the segment's anchor. Ranges are clamped to the original line.
- **Diagnostic message text:** left as clangd wrote it. Possible later polish: rewrite `co_return` to `return` in messages.

## Acceptance criteria (draft)

- [ ] `lsp_proxy_tests` with `fake_lsp_server`:
  - the server receives lowered text for a buffer containing `async`, and identical text for a buffer without markers;
  - a scripted `publishDiagnostics` at a lowered position (for example the column of `nope` after `co_await (f())`) arrives at the editor with the original column (37 in the `col.cpp` example, as an LSP 0-based character of 36);
  - an incremental `didChange` (insert one character) produces a full-text `didChange` to the server with the re-lowered text;
  - an asynctp tolerant diagnostic appears with `source: "asynctp"`.
- [ ] Optional real-clangd test (skips with code 77 when clangd is absent): opening the `hello` example gives zero diagnostics.
- [ ] `verify.sh all` passes.

## Notes (open questions)

- D1 no longer blocks this ticket (resolved). What still blocks `ready` is spike T-0018:
  - **Q2:** does clangd 21 honour `positionEncodings: ["utf-8"]`, and what do VS Code and Neovim advertise? This decides whether the proxy converts UTF-16 itself, which is part of the core data path.
  - **Q5:** is full-text `didChange` per keystroke acceptable on a 2,000-line file? This decides whether full sync is enough or incremental sync must be emitted.
- Should diagnostics clangd raises *only because of lowering artifacts* be filtered? Decide after observing real output.

## Log

- 2026-10-17 architect: created under epic T-0009; proposed pending T-0018.
- 2026-10-17 architect: D1 resolved (POSIX allowed in transpiler/src/platform/; T-0021 now ready). Still proposed: needs spike answers Q2 (position encoding) and Q5 (full-sync latency) before the blueprint is final.
