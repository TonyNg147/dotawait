# Tickets

The backlog for dotawait. One markdown file per ticket,
`T-NNNN-<slug>.md`, with front matter (`id`, `title`, `status`, `area`,
`type`, `priority`). Create and move tickets with the script so the table
below stays in sync:

```sh
python3 .claude/skills/ticket/scripts/ticket.py new "Support noexcept on async fns" --area transpiler --type limitation
python3 .claude/skills/ticket/scripts/ticket.py list --status ready
python3 .claude/skills/ticket/scripts/ticket.py status T-0003 in-progress
```

## Status flow

```
proposed ──architect──> ready ──developer──> in-progress ──> in-review ──tester──> done
    └──────────────────> rejected (with reason in Log)          ^   │ fail
                                                                │   v
                                                             in-progress
```

| Status | Meaning | Who moves it |
|---|---|---|
| `proposed` | Idea or limitation captured; not yet designed | anyone |
| `ready` | Has a blueprint and testable acceptance criteria | architect |
| `in-progress` | Being implemented | developer |
| `in-review` | Implementation done, `verify.sh full` green | developer |
| `done` | Tester verdict PASS, `verify.sh all` green | tester (via `/ship`) |
| `rejected` | Won't do; reason recorded in Log | architect |

Priorities: `P1` blocks real-world use, `P2` notable friction, `P3` nice to have.

## Index

<!-- BEGIN TICKET INDEX -->
| ID | Title | Status | Area | Type | Pri |
|---|---|---|---|---|---|
| [T-0001](T-0001-return-in-braceless-if-else-case-not-lowered.md) | return in braceless if/else/loop/case is not lowered to co_return | ready | transpiler | bug | P1 |
| [T-0002](T-0002-noexcept-and-trailing-return-type-on-async-fns.md) | Support noexcept and trailing return type on async functions | ready | transpiler | limitation | P1 |
| [T-0003](T-0003-await-backward-scan-silently-corrupts-string-literals.md) | Define the .await operand grammar (chains, template args, keywords) and reject what it cannot parse | ready | transpiler | bug | P1 |
| [T-0004](T-0004-identifier-named-async-silently-mis-lowered.md) | Recognise async only at declaration starts; leave std::async and other identifiers alone | ready | transpiler | bug | P1 |
| [T-0006](T-0006-async-declarations-in-headers.md) | Parse async declarations, qualified names, and bounded signatures | ready | transpiler | feature | P1 |
| [T-0010](T-0010-add-a-shared-tokenizer-and-port-marker-scan-and.md) | Add a shared tokenizer and port marker scan and signature parser to it | ready | transpiler | bug | P1 |
| [T-0011](T-0011-port-lower-body-to-the-token-stream-and-delete-s.md) | Port lower_body to the token stream and delete SourceCursor | ready | transpiler | chore | P1 |
| [T-0012](T-0012-record-an-orig-to-generated-source-map-and-emit.md) | Record an orig-to-generated source map and emit exact positions | ready | transpiler | bug | P1 |
| [T-0013](T-0013-guarantee-every-async-function-is-a-coroutine.md) | Guarantee every async function is a coroutine | ready | transpiler | bug | P1 |
| [T-0014](T-0014-add-the-asynctp-cc-compiler-launcher.md) | Add the asynctp-cc compiler launcher | ready | transpiler | feature | P1 |
| [T-0015](T-0015-switch-cmake-and-makefile-integration-to-the-lau.md) | Switch CMake and Makefile integration to the launcher | ready | cmake | feature | P1 |
| [T-0018](T-0018-spike-verify-clangd-behaviour-the-lsp-proxy-depe.md) | Spike: verify clangd behaviour the LSP proxy depends on | ready | transpiler | chore | P1 |
| [T-0020](T-0020-add-a-tolerant-lowering-mode-for-half-typed-buff.md) | Add a tolerant lowering mode for half-typed buffers | ready | transpiler | feature | P1 |
| [T-0021](T-0021-add-asynctp-lsp-as-a-transparent-clangd-proxy.md) | Add asynctp-lsp as a transparent clangd proxy | ready | transpiler | feature | P1 |
| [T-0026](T-0026-add-the-ci-yml-build-and-verify-workflow.md) | Add the ci.yml build and verify workflow | ready | cross-cutting | chore | P1 |
| [T-0005](T-0005-lambda-start-detection-misses-keywords-and-literals.md) | Lambda-start detection misreads [ after most keywords | ready | transpiler | limitation | P2 |
| [T-0017](T-0017-drop-the-async-cpp-naming-from-examples-fixtures.md) | Drop the .async.cpp naming from examples, fixtures, and docs | ready | cross-cutting | chore | P2 |
| [T-0027](T-0027-make-golden-expected-outputs-compile-and-check-i.md) | Make golden expected outputs compile and check it in verify | ready | cross-cutting | bug | P2 |
| [T-0028](T-0028-add-tools-ci-and-the-zero-dependency-include-che.md) | Add tools/ci and the zero-dependency include check | ready | cross-cutting | chore | P2 |
| [T-0029](T-0029-add-the-agent-pr-scope-guard.md) | Add the agent-PR scope guard | ready | cross-cutting | feature | P2 |
| [T-0030](T-0030-add-ticket-evidence-markers-and-the-ticket-consi.md) | Add ticket evidence markers and the ticket-consistency gate | ready | cross-cutting | feature | P2 |
| [T-0031](T-0031-add-the-tests-fail-on-base-gate-for-agent-prs.md) | Add the tests-fail-on-base gate for agent PRs | ready | cross-cutting | feature | P2 |
| [T-0016](T-0016-remap-build-diagnostic-columns-through-the-sourc.md) | Remap build diagnostic columns through the source map | ready | transpiler | feature | P3 |
| [T-0009](T-0009-epic-suffix-free-build-via-compiler-launcher-and.md) | Epic: suffix-free build via compiler launcher and LSP proxy | proposed | cross-cutting | feature | P1 |
| [T-0022](T-0022-lower-open-buffers-in-asynctp-lsp-and-map-diagno.md) | Lower open buffers in asynctp-lsp and map diagnostics | proposed | transpiler | feature | P1 |
| [T-0023](T-0023-map-positions-for-completion-hover-and-navigatio.md) | Map positions for completion, hover, and navigation in asynctp-lsp | proposed | transpiler | feature | P1 |
| [T-0024](T-0024-serve-lowered-headers-to-clangd-in-asynctp-lsp.md) | Serve lowered headers to clangd in asynctp-lsp | proposed | transpiler | feature | P1 |
| [T-0025](T-0025-epic-ci-agent-pr-gates-release-and-repo-defences.md) | Epic: CI, agent-PR gates, release, and repo defences | proposed | cross-cutting | feature | P1 |
| [T-0032](T-0032-apply-repo-defences-codeowners-rulesets-agent-id.md) | Apply repo defences: CODEOWNERS, rulesets, agent identity, Actions settings | proposed | cross-cutting | chore | P1 |
| [T-0007](T-0007-join-combinator.md) | join combinator to await several tasks concurrently | proposed | runtime | feature | P3 |
| [T-0008](T-0008-async-lambdas.md) | Async lambdas / closures | proposed | transpiler | feature | P3 |
| [T-0033](T-0033-add-release-yml-source-and-binary-release-on-v-t.md) | Add release.yml: source and binary release on v* tags | proposed | cross-cutting | feature | P3 |
| [T-0019](T-0019-add-zero-dependency-json-and-json-rpc-framing.md) | Add zero-dependency JSON and JSON-RPC framing | done | transpiler | feature | P1 |
<!-- END TICKET INDEX -->
