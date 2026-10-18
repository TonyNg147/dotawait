---
id: T-0023
title: Map positions for completion, hover, and navigation in asynctp-lsp
status: proposed
area: transpiler
type: feature
priority: P1
depends_on: [T-0022, T-0018]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0022, T-0018 (Q3 message inventory, Q4 formatting, Q6 `.await` completion).

## User story

As a C++ developer, I want completion, hover, signature help,
go-to-definition and find-references to work in async functions exactly
as in ordinary code, including member completion after `x.await.`, so
that "LSP and autocompletion support" (the user's mandatory requirement)
is actually met.

## Scope

- In: every request/response the spike's inventory (Q3) marks **map**:
  - requests: positions original → generated with `to_generated` (Bias::Start);
  - responses: ranges generated → original, for the same document;
  - `Location`/`LocationLink` into *other* documents: identity, unless the target is a lowered document the proxy holds (then its map), or a header (T-0024);
  - completion `textEdit`/`additionalTextEdits`;
  - `WorkspaceEdit` (rename, code actions).
- **Block** list: requests the inventory marks unsafe (formatting on lowered text, unless Q4 finds a safe path; semantic tokens, unless mappable). Answer them with an empty result, or remove the capability from the `initialize` result, and log why.
- **Fail loudly:** an edit that overlaps a non-verbatim segment (it would modify generated code) is rejected with a JSON-RPC error `"asynctp-lsp: edit touches lowered code at L:C"`. It is never applied partially.
- Possibly: inject a completion item `await` after `.` inside async functions (spike Q6).
- Out: headers (T-0024).

## Acceptance criteria (draft)

- [ ] `lsp_proxy_tests` with scripted fake-server responses, for each mapped method: request positions are translated, and response ranges come back to original coordinates for a buffer with `.await` and `return` rewrites on the same line.
- [ ] Edit overlapping `co_await (` gives a JSON-RPC error; a verbatim-only edit is mapped and forwarded.
- [ ] Blocked capabilities are absent from the `initialize` result seen by the editor.
- [ ] Optional real-clangd tests (skip 77): completion after `x.await.` lists a member of the awaited type; hover over a call to an `async int` function shows its signature; definition jumps to the right line/col in the same file.
- [ ] `verify.sh all` passes.

## Notes (open questions)

D1 no longer blocks this ticket (resolved). What still blocks `ready` is spike T-0018:
- **Q3:** the full map/pass/block inventory of methods and fields for clangd 21.
- **Q4:** what formatting returns on a lowered buffer, and whether it can be mapped safely or must be blocked.
- **Q6:** what clangd completes after `x.` and `(co_await (x)).`, which decides whether to inject an `await` item.

## Log

- 2026-10-17 architect: created under epic T-0009; proposed pending T-0018.
- 2026-10-17 architect: D1 resolved (POSIX allowed in transpiler/src/platform/; T-0021 now ready). Still proposed: needs spike answers Q3 (message inventory), Q4 (formatting), Q6 (`.await` completion).
