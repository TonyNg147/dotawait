---
id: T-0016
title: Remap build diagnostic columns through the source map
status: ready
area: transpiler
type: feature
priority: P3
depends_on: [T-0012, T-0014]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0012 (source map, line preservation), T-0014 (launcher).

## User story

As a C++ developer, I want GCC's caret to sit under the token I wrote, not
somewhere past the end of my line, so that build errors in async code look
exactly like errors in ordinary code.

## Problem

After T-0012, lines are exact, but columns refer to the lowered text.
Observed (input line 2: `async int g() { int z = f().await + nope; return z; }`):

```
col.cpp:2:53: error: 'nope' was not declared in this scope
    2 | async int g() { int z = f().await + nope; return z; }
      |                                                     ^
```

`nope` is at column 37. GCC prints the *original* line (it reads the
`#line` file) but places the caret at the lowered column.

## Scope

- In: in `asynctp-cc`, when at least one file was lowered, capture the compiler's stderr and rewrite:
  - `<file>:<line>:<col>:` prefixes for files the launcher lowered in this invocation;
  - the caret line that follows a quoted source line.
- Out:
  - JSON/SARIF diagnostics (`-fdiagnostics-format=…`): the launcher leaves stderr untouched when that flag is present.
  - Range tildes (`~~~`) are dropped rather than remapped.
  - Diagnostics in files that were only mirrored, not lowered: their columns are already right.
- Touches: `transpiler/src/launcher.cpp`, `launcher.hpp`, `launcher_main.cpp`, `launcher_tests.cpp`, `launcher_integration_tests.cpp`.

## Blueprint

1. **Keep the maps.** `launcher_main.cpp` keeps, per lowered file, its `EmitResult` (text + `SourceMap`) keyed by the absolute original path.
2. **Run the compiler with stderr captured** (`run_command` gains `std::optional<std::filesystem::path> stderr_to`; the shell form appends `2> '<file>'`). Afterwards, rewrite the captured text and write it to `std::cerr`.
   - Skip capture entirely when nothing was lowered, when `-fdiagnostics-format` is present, or when env `ASYNCTP_CC_REMAP=0`.
3. **Pure function**
   `std::string remap_diagnostics(std::string_view stderr_text, const std::map<std::string, const EmitResult*>&, std::size_t header_lines)`.
   `header_lines` is the number of generated lines before the original line 1 (prelude + `#line`, plus 0 for a BOM).
   - For each line, match an optional ANSI SGR prefix, then `<path>:<L>:<C>:`. If `<path>` is in the map:
     - generated offset = start of generated line `L + header_lines` + `(C-1)` bytes;
     - `o = map.to_original(offset)`;
     - original column = `o - start_of_original_line(L) + 1`;
     - replace `C`.
   - **Caret line:** if the following two lines are GCC's quoted-source line (`<spaces><L> | <text>`) and caret line (`<spaces> | <spaces>^…`), rebuild the caret line as `<same prefix> | ` + `(origcol-1)` spaces + `^`. Tabs in the quoted text are expanded by GCC, so count display columns from the quoted text rather than bytes.
   - Lines that do not match pass through unchanged.
4. **Color.** Capturing stderr makes GCC stop auto-coloring. Document this. If the user passed `-fdiagnostics-color=always`, the matcher tolerates SGR sequences around the path and numbers.
5. Document the behaviour and the `ASYNCTP_CC_REMAP` switch in the `launcher.hpp` header comment.

### Invariants

- **Fail loudly:** an unmatched or unmappable line is passed through unchanged, never dropped.
- **Other invariants:** unaffected (launcher-only, standard library only).

## Acceptance criteria

- [ ] `launcher_tests`: `remap_diagnostics` on the exact three-line GCC output above, with the `col.cpp` map, gives `col.cpp:2:37:` and a caret under `n` of `nope`.
  Plus:
  - an SGR-colored variant;
  - a line for a non-lowered file (unchanged);
  - a note line (`note:`) mapped the same way;
  - a diagnostic on a line with no edits (column unchanged).
- [ ] `launcher_integration_tests`: compiling `col.cpp` through `asynctp-cc` prints `col.cpp:2:37: error`; with `ASYNCTP_CC_REMAP=0` it prints `:2:53:`.
- [ ] `launcher.hpp` comment updated. README "Known MVP limitations" gains "build diagnostics lose GCC's automatic colouring when asynctp-cc remaps columns (set `ASYNCTP_CC_REMAP=0` or pass `-fdiagnostics-color=always`)".
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Log

- 2026-10-17 architect: created under epic T-0009; column drift reproduced (53 vs 37); ready.
