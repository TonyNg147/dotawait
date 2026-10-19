---
id: T-0019
title: Add zero-dependency JSON and JSON-RPC framing
status: done
area: transpiler
type: feature
priority: P1
depends_on: []
created: 2026-10-17
---

Epic: T-0009. Depends on: none (it can start in parallel with T-0010).

## User story

As the maintainer of `asynctp-lsp`, I need a small, tested JSON value type
and LSP base-protocol framing in the standard library only, so that the
proxy honours the zero-dependency invariant (no nlohmann/json, no Boost).

## Problem

There is no JSON or JSON-RPC code in the repo. `asynctp-lsp` (T-0021) must
read and write LSP messages:
`Content-Length: N\r\n\r\n<N bytes of UTF-8 JSON>`.
CLAUDE.md forbids third-party libraries in `transpiler/`.

## Scope

- In: a JSON DOM (parse, serialize, accessors) and LSP framing over
  `std::istream`/`std::ostream`, with tests.
- Out:
  - Process spawning and threads (T-0021).
  - Schema types for LSP messages: handled ad hoc in T-0022/T-0023, where only a few fields are touched.
  - JSON pointer/patch, comments, trailing commas: not needed.
- Touches:
  - `transpiler/include/asynctp/lsp/json.hpp`, `transpiler/src/lsp/json.cpp` (new)
  - `transpiler/include/asynctp/lsp/framing.hpp`, `transpiler/src/lsp/framing.cpp` (new)
  - `transpiler/CMakeLists.txt`, `Makefile`
  - `transpiler/tests/json_tests.cpp`, `transpiler/tests/framing_tests.cpp` (new)

## Blueprint

1. **`json.hpp`** (namespace `asynctp::lsp`):
   ```cpp
   class Json {
   public:
     using Array = std::vector<Json>;
     using Object = std::vector<std::pair<std::string, Json>>; // insertion order preserved: forwarded messages stay recognisable in logs
     // null, bool, int64 (integers without fraction/exponent, exact for LSP ids/positions), double, string, Array, Object
     static std::variant<Json, JsonError> parse(std::string_view);
     std::string dump() const;                 // compact, no spaces
     // accessors: is_*(), as_*(), find(key) -> Json* / const Json*, operator[](key) inserting, push_back
   private:
     std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object> v_;
   };
   struct JsonError { std::size_t offset; std::string message; };
   ```
   - **Parse:** RFC 8259 strict.
     - Escapes `\" \\ \/ \b \f \n \r \t \uXXXX`, with surrogate pairs combined to UTF-8. A lone surrogate is a `JsonError`.
     - Numbers: integer when there is no `.`/`e` and the value fits `int64`; otherwise `double`.
     - Max nesting depth 256: deeper input is a `JsonError` (protects against stack exhaustion).
   - **Dump:**
     - escape `"`, `\`, and control characters < 0x20 as `\u00XX`;
     - pass non-ASCII UTF-8 through unchanged;
     - doubles with `std::to_chars` (shortest round-trip); int64 exactly.
   - No exceptions (transpiler convention). Accessors on the wrong type return `nullptr` or a default; document this.
2. **`framing.hpp`:**
   ```cpp
   std::optional<std::string> read_message(std::istream&, std::string* error); // nullopt on EOF or error
   void write_message(std::ostream&, std::string_view json);                  // writes header + body, flushes
   ```
   - Read headers until an empty line (`\r\n`). Accept a `Content-Type` header and ignore it. `Content-Length` is required (case-insensitive name).
   - Read exactly N bytes.
   - Malformed header or short body: `nullopt` plus an `error` message.
   - Callers must open streams in binary mode. On POSIX, `std::cin`/`std::cout` are binary; document that Windows is out of scope.
3. **Build parity.** Add both `.cpp` to `asynctp_core` (unconditional after T-0014; if T-0014 has not landed, add them to the test-only `asynctp_core` and to `TRANSPILER_SRCS`). Add both suites to CMake and the Makefile.

### Invariants

- **Zero-dependency:** standard library only. This is the reason the ticket exists.
- **Transpiler/runtime independence:** unaffected (pure transpiler/ code).
- **Makefile/CMake parity:** two sources and two suites in both.

## Acceptance criteria

- [ ] `json_tests`:
  - round-trip `{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":null,"capabilities":{}}}` byte-for-byte through `parse` + `dump`;
  - integers `0`, `-1`, `9007199254740993` (exact as int64); doubles `1.5`, `1e-3`;
  - `"é"` becomes UTF-8 `é`; `"😀"` becomes 😀; lone `"\ud83d"` is a `JsonError`;
  - control-character escaping on dump;
  - key order preserved;
  - errors with offsets for `{"a":}`, `[1,]`, `tru`, and an unterminated string;
  - depth 257 nested arrays is a `JsonError`.
- [ ] `framing_tests`:
  - two back-to-back messages in one `std::stringstream` are read in order;
  - `content-length` lowercase is accepted;
  - a `Content-Type` header is ignored;
  - a missing length gives an error;
  - a truncated body gives an error;
  - `write_message` output is exactly `Content-Length: <n>\r\n\r\n<body>`, with n in **bytes** (test with a non-ASCII body).
- [ ] Both suites are built and run by CMake and the Makefile; ASan clean.
- [ ] Header comments document the no-exceptions accessor contract and the Windows binary-mode limitation.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Log

- 2026-10-17 architect: created under epic T-0009; ready (no clangd needed).
- 2026-10-18 developer: added asynctp::lsp::Json (strict RFC 8259 parse incl. UTF-8 validation, depth 256, int64/double split; compact dump) and read_message/write_message framing; json_tests + framing_tests in CMake and Makefile (T-0014 not landed, so sources go in test-only asynctp_core + TRANSPILER_SRCS). verify full and asan OK; in-review.
- 2026-10-18 tester: PASS. 5/5 criteria. Gap closed: the existing "é"/"😀" tests used raw UTF-8, so the \uXXXX decoder and surrogate-pair combining had no coverage; added json_tests cases for é, 😀, every UTF-8 length boundary, lone-surrogate variants, number grammar, invalid UTF-8 and BOM, plus framing_tests for multi-chunk (>64 KiB) bodies, a SIZE_MAX length on a short stream, and header edge cases. All pass; verify all OK.
