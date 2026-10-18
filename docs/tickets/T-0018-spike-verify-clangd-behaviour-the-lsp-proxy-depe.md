---
id: T-0018
title: Spike: verify clangd behaviour the LSP proxy depends on
status: ready
area: transpiler
type: chore
priority: P1
depends_on: []
created: 2026-10-17
---

Epic: T-0009. Depends on: nothing in code. Prerequisite: clangd installed on the dev machine (T-0009 decision D4). It is **not installed** today: `which clangd` prints nothing, and `apt-cache policy clangd` offers candidate `1:21.1.6-71+b1`.

## User story

As the architect, I need verified answers about clangd's behaviour before
T-0021 to T-0024 can become `ready`, so that the proxy design rests on
experiment rather than memory.

## Problem

The user decided on an LSP proxy: `asynctp-lsp` sits between the editor
and clangd, lowers buffers in memory, and maps positions back. Several
design choices depend on clangd behaviour the architect could not observe,
because clangd is absent on this machine. The biggest open question is how
clangd should see **lowered headers**.

GCC experiments show that quoted includes resolve relative to the physical
file. For clangd, the main file keeps its original URI, so
`#include "jobs.hpp"` would find the *original*, unlowered header on disk.

## Scope

- In: time-boxed experiments (1-2 days) with clangd 21, a scratch project, and a tiny scripted LSP client. Python or a shell + `jq` driver is fine **for the spike only**; it is not committed under `transpiler/`.
- Out: any production code.
- Deliverable: `docs/design/lsp-proxy.md` (new) answering every question below with the evidence (commands plus trimmed JSON transcripts) and a recommendation. The scratch client may be kept under `docs/design/lsp-spike/` for reproducibility.

## Questions to answer (each with evidence)

1. **Header overlay (decides T-0024).** Which of these makes clangd parse `main.cpp` (lowered, sent via `didOpen`) against a **lowered** `jobs.hpp` without writing into the source tree?
   - (a) `didOpen` the header's URI with lowered text: does clangd use open-document content when *another* TU includes that file, including inside the preamble? What happens on `didChange` to the header?
   - (b) `-ivfsoverlay <yaml>` added to the compile command (via a proxy-generated `compile_commands.json` and `--compile-commands-dir`) that maps original header paths to lowered copies under the build dir.
   - (c) Present the main file to clangd under a shadow URI (the build tree's mirrored path, as in T-0014) with a generated compile database, and rewrite URIs both ways.

   For each: does go-to-definition into the header return the original URI or the shadow/overlay one, and are positions in lowered or original coordinates?
2. **Position encoding.** Does clangd 21 honour `general.positionEncodings: ["utf-8"]` (LSP 3.17)? Which encodings do VS Code (vscode-clangd) and Neovim 0.11 advertise? Decide whether the proxy converts UTF-16 itself.
3. **Message inventory.** For clangd 21's advertised capabilities, list every request, response and notification field that carries a URI, Position, Range, TextEdit or WorkspaceEdit. Mark each **map**, **pass**, or **block**. At minimum consider:
   - `publishDiagnostics` (incl. `relatedInformation`, `codeActions` extension);
   - completion (`textEdit`, `insertReplaceEdit`, `additionalTextEdits`), `resolve`;
   - hover, signatureHelp;
   - definition, declaration, typeDefinition, implementation (`Location` vs `LocationLink`);
   - references, documentHighlight, documentSymbol, foldingRange, selectionRange;
   - rename / prepareRename, codeAction / `workspace/applyEdit`;
   - formatting / rangeFormatting / onTypeFormatting;
   - semanticTokens (full/delta), inlayHint, callHierarchy, typeHierarchy;
   - clangd extensions (`textDocument/switchSourceHeader`, `textDocument/symbolInfo`, `textDocument/ast`, `$/memoryUsage`, `textDocument/inactiveRegions`).
4. **Formatting.** clangd formats the text it holds, which would be the lowered text. What does `textDocument/formatting` return on a lowered buffer, and is formatting the original text feasible (for example a second, unlowered virtual document)? Recommend: map, or block with a message.
5. **Sync cost.** Full-text `didChange` on every edit for a 2,000-line file: clangd's diagnostic latency compared with incremental sync. Is full sync acceptable?
6. **`.await` completion.** With the buffer lowered to `(co_await (x)).` (T-0003's parenthesisation, T-0020's tolerant mode), does clangd complete members of the awaited `T`? What does it return right after `x.` inside an async function? This decides whether T-0023 injects an `await` item.
7. **Editor launch.**
   - Does vscode-clangd run `<clangd.path> --version` and require clangd-shaped output?
   - Does Neovim's built-in `vim.lsp.config('clangd', { cmd = {...} })` need anything special?
   - How should `asynctp-lsp` forward `--version`/`--help`?
8. **Discovery.** Confirm a safe rule for finding the real clangd (flag, then env, then `PATH`, then `clangd-NN`) that never resolves to `asynctp-lsp` itself, for example when a user symlinks it as `clangd`.

## Acceptance criteria

- [ ] `docs/design/lsp-proxy.md` exists and answers questions 1-8, each with the commands run and the observed JSON (trimmed), plus a recommendation for 1, 2, 4 and 6.
- [ ] The architect updates T-0021 to T-0024 from the findings (each ticket's Log records it) and moves those that are complete to `ready`.
- [ ] No files are changed outside `docs/`.

## Notes

- The architect's current leaning for Q1 is (a), with (b) as fallback: it keeps original URIs and needs no source-tree or compile-DB rewriting. It is unverified, so it must not be built until the spike confirms it.
- The real-clangd integration tests in T-0021 onward must **skip, not fail**, when clangd is absent (see T-0021).

## Log

- 2026-10-17 architect: created under epic T-0009; clangd not installed (21.1.6 available via apt); ready as a spike pending decision D4.
- 2026-10-17 architect: D4 resolved (the human installs clangd 21.1.6 via apt; real-clangd tests skip with 77 when absent). T-0021 no longer waits for this spike: Q7/Q8 are fixed by design there, and the spike only confirms them. Q1-Q6 still gate T-0022/T-0023/T-0024.
