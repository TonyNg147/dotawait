---
id: T-0021
title: Add asynctp-lsp as a transparent clangd proxy
status: ready
area: transpiler
type: feature
priority: P1
depends_on: [T-0019]
created: 2026-10-17
---

Epic: T-0009. Dependency notes: T-0019 (JSON + framing) is required. D1 is resolved (POSIX allowed in `transpiler/src/platform/`). Spike T-0018 is **not** required: this ticket fixes the design for spike questions Q7 (`--version`) and Q8 (discovery), and the optional real-clangd smoke test verifies them.

## User story

As a C++ developer, I want to point my editor's clangd setting at
`asynctp-lsp` and get exactly today's clangd experience, so that the proxy
can then be taught about `async` (T-0022 to T-0024) without me changing my
editor config again.

## Problem

There is no LSP component. The user chose a proxy (T-0009). This first
stage owns the clangd child process and forwards every message unchanged
in both directions. Positions are not transformed yet.

clangd is not installed on the architect's machine (`which clangd` prints
nothing; apt candidate 21.1.6). The human will install it, and every test
that needs real clangd must skip with exit code 77 when it is absent
(decision D4).

## Scope

- In:
  - a `platform` process module (POSIX);
  - the `asynctp-lsp` executable;
  - clangd discovery and launch;
  - two pump threads;
  - lifecycle and crash handling;
  - a hook seam for later tickets;
  - `--version`/`--help`;
  - a fake LSP server and tests;
  - an optional real-clangd smoke test;
  - a README "Editor / LSP" section.
- Out: lowering and position mapping (T-0022 to T-0024); Windows (compile-time `#error`).
- Touches:
  - `transpiler/include/asynctp/platform/process.hpp` (new), `transpiler/src/platform/process_posix.cpp` (new)
  - `transpiler/include/asynctp/lsp/proxy.hpp` (new), `transpiler/src/lsp/proxy.cpp` (new), `transpiler/src/lsp/lsp_main.cpp` (new)
  - `transpiler/tests/fake_lsp_server.cpp` (new), `transpiler/tests/lsp_proxy_tests.cpp` (new), `transpiler/tests/lsp_clangd_smoke.cpp` (new)
  - `transpiler/CMakeLists.txt`, `Makefile`, `README.md`

## Blueprint

1. **`platform/process.hpp`** (namespace `asynctp::platform`). This is the only place POSIX headers may be included (D1):
   ```cpp
   struct Child { int pid = -1; int in_fd = -1; int out_fd = -1; };   // child's stdin (we write) / stdout (we read)
   std::variant<Child, std::string> spawn_with_pipes(const std::vector<std::string>& argv); // stderr inherited
   int wait_child(Child&, std::chrono::milliseconds timeout);         // exit code; 128+sig if signalled; SIGTERM after timeout
   void close_fd(int& fd);
   bool write_all(int fd, std::string_view bytes);                   // loops on partial writes / EINTR
   class FdInStreambuf : public std::streambuf { /* buffered read(2), for framing::read_message */ };
   std::optional<std::filesystem::path> self_executable();           // canonical("/proc/self/exe"); macOS: _NSGetExecutablePath
   ```
   - Implement in `process_posix.cpp` with `pipe`, `fork`, `execvp` and `waitpid`. Use `_exit(127)` after a failed exec; the parent reports "could not execute <path>".
   - Set `signal(SIGPIPE, SIG_IGN)` once in `main`, so a dead clangd surfaces as a write error, not a kill.
   - Guard the file with `#if !defined(__unix__) && !defined(__APPLE__)` then `#error "asynctp-lsp needs POSIX (see T-0009 D1)"`.
2. **Discovery** (pure, unit-testable, in `proxy.hpp`):
   ```cpp
   std::optional<std::filesystem::path> discover_clangd(
       std::optional<std::string> flag, std::optional<std::string> env_value,
       const std::vector<std::filesystem::path>& path_dirs, const std::optional<std::filesystem::path>& self);
   ```
   Order:
   1. `--clangd=<path>`;
   2. env `ASYNCTP_CLANGD`;
   3. the first executable `clangd` in `PATH` order whose `weakly_canonical` path differs from `self`. This skips a user symlink of `asynctp-lsp` named `clangd`;
   4. the highest-numbered executable `clangd-NN` in any `PATH` dir.

   An explicit flag or env value pointing at `self` is an error. `main` builds `path_dirs` by splitting `std::getenv("PATH")` on `:`.
3. **CLI** (`lsp_main.cpp`): `asynctp-lsp [--clangd=<path>] [--log=<file>] [--] [args forwarded to clangd...]`. Unknown `--foo` options before `--` are also forwarded to clangd, so users can keep `--background-index` etc. in the same place.
   - **`--version`:** run `<clangd> --version` through `spawn_with_pipes` and copy its stdout verbatim first. Editor extensions that parse `clangd version X` keep working. Then append a line `asynctp-lsp <project version> (proxying <clangd path>)`. Exit 0, or 1 if clangd is not found.
   - **`--help`:** asynctp-lsp usage, then a note that other args go to clangd.
4. **Proxy core** (`proxy.hpp/.cpp`):
   ```cpp
   struct Hooks {  // identity when empty; returning nullopt swallows the message
     std::function<std::optional<std::string>(std::string&& body)> client_to_server, server_to_client;
   };
   int run_proxy(const std::filesystem::path& clangd, const std::vector<std::string>& clangd_args,
                 std::istream& client_in, std::ostream& client_out, const Hooks&, std::ostream* log);
   ```
   - **Thread A** reads framed bodies from `client_in` (`read_message`, T-0019), applies the hook, and writes to the clangd `in_fd` (`write_all`) under `server_mutex`.
   - **Thread B** reads from clangd through `FdInStreambuf`, applies the hook, and writes to `client_out` (`write_message`) under `client_mutex`.
   - With empty hooks, the **raw body bytes are forwarded without parsing**. This makes byte-identity testable and costs nothing.
   - Even with identity hooks, the proxy parses each **client** body with `Json` (never substring matching) for two things only: to detect `"method":"exit"` and to remember the `initialize` request id. Server bodies are not parsed.
   - **Client EOF, or `exit` forwarded:** close `in_fd`, `wait_child(2s)`, return clangd's exit code. Return 1 if `exit` never arrived (LSP spec: exit without shutdown means 1).
   - **clangd stdout EOF before `exit` was forwarded:** send the client `window/showMessage` `{type:1, message:"asynctp-lsp: clangd exited unexpectedly (status N); see the clangd log"}`, then return 1.
   - **Spawn failure or clangd not found:**
     - answer the client's `initialize` request with JSON-RPC error `{code:-32603, message:"asynctp-lsp: could not find clangd; pass --clangd=<path> or set ASYNCTP_CLANGD"}`;
     - also send the same text via `window/showMessage` type 1;
     - keep answering requests with that error until `exit`, then return 1.

     This is fail loudly, inside the editor.
   - **`--log`:** one line per message: `<direction> <method or id> <bytes>`. Never log bodies by default.
5. **`fake_lsp_server.cpp`** (test helper, standard library plus framing):
   - reads framed messages from stdin, appends each raw body plus `\n` to the file in env `FAKE_LSP_TRANSCRIPT`;
   - `initialize`: replies `{"jsonrpc":"2.0","id":<id>,"result":{"capabilities":{}}}`;
   - `shutdown`: `result: null`;
   - `exit`: exits 0;
   - `fake/echo` (notification): sends notification `fake/echoed` with the same params;
   - `fake/crash`: exits 3 immediately;
   - `--version`: prints `clangd version 99.0.0 (fake)`.
6. **Build parity:**
   - CMake: add `src/platform/process_posix.cpp` and `src/lsp/proxy.cpp` to `asynctp_core`; add `add_executable(asynctp-lsp src/lsp/lsp_main.cpp)` linking `asynctp_core` and `Threads::Threads` (`find_package(Threads REQUIRED)`). This is the platform thread library, allowed under D1.
   - Tests: `lsp_proxy_tests` receives `$<TARGET_FILE:asynctp-lsp> $<TARGET_FILE:fake_lsp_server>`. `lsp_clangd_smoke` gets `set_tests_properties(lsp_clangd_smoke PROPERTIES SKIP_RETURN_CODE 77)`.
   - Makefile: the same binaries with `-pthread`. The `test` target runs `lsp_clangd_smoke` and treats exit 77 as "SKIPPED" (print the message, continue).
7. **README "Editor / LSP" section.**
   - VS Code (vscode-clangd): `"clangd.path": "/path/to/build/transpiler/asynctp-lsp"`, `"clangd.arguments": ["--clangd=/usr/bin/clangd", "--compile-commands-dir=build"]`.
   - Neovim 0.11: `vim.lsp.config('clangd', { cmd = { '/path/to/asynctp-lsp', '--clangd=/usr/bin/clangd' } })`, or nvim-lspconfig `require('lspconfig').clangd.setup{ cmd = { ... } }`.
   - Helix (`languages.toml`): `[language-server.clangd]` `command = "asynctp-lsp"`.
   - Emacs eglot: `(add-to-list 'eglot-server-programs '((c++-mode c-mode) "asynctp-lsp"))`.
   - State that this stage is a pure pass-through, so async files still show errors until T-0022.

### Invariants

- **Transpiler/runtime independence:** `asynctp-lsp` includes nothing from `runtime/`.
- **Zero-dependency (as amended by D1):** standard library plus POSIX, only in `transpiler/src/platform/`. `grep -rn '#include <unistd.h>\|<sys/' transpiler/` must match only `src/platform/`.
- **Fail loudly:** missing clangd, a clangd crash, and exec failure all reach the editor as visible errors.
- **Makefile/CMake parity:** two binaries plus three test executables in both.

## Acceptance criteria

- [ ] `lsp_proxy_tests` (unit part): `discover_clangd` cases:
  - flag beats env, which beats PATH, which beats `clangd-NN`;
  - a PATH `clangd` that canonicalises to `self` is skipped;
  - a flag pointing at `self` is an error;
  - `clangd-14` and `clangd-21` in PATH pick `clangd-21`;
  - nothing found gives nullopt.

  Use temp dirs with executable files (`std::filesystem::permissions`).
- [ ] `lsp_proxy_tests` (process part, spawning `asynctp-lsp --clangd=<fake>`):
  - initialize, then `fake/echo`, then shutdown, then exit. The transcript holds the exact bytes sent. The client receives the initialize result, `fake/echoed` and the shutdown result byte-identically. The proxy exit code is 0.
  - `fake/crash`: the client receives `window/showMessage` with `clangd exited unexpectedly`; the proxy exit code is 1.
  - `--clangd=/nonexistent`: the initialize response is a JSON-RPC error whose message contains `could not find clangd` (or `could not execute`); the exit code after `exit` is 1.
  - `asynctp-lsp --clangd=<fake> --version` stdout starts with `clangd version 99.0.0 (fake)` and contains `asynctp-lsp`.
- [ ] `lsp_clangd_smoke`:
  - exits 77 with `clangd not found; skipping` when discovery finds nothing;
  - otherwise checks that `--version` contains `clangd version`, then runs initialize/shutdown/exit through the proxy against real clangd and expects exit 0.
- [ ] The grep in the Invariants section shows POSIX includes only under `transpiler/src/platform/`.
- [ ] ASan/UBSan clean (`verify.sh asan`); warning-free under `-Wall -Wextra`.
- [ ] README "Editor / LSP" section per step 7.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Notes

- Spike T-0018 still records the real vscode-clangd `--version` behaviour (Q7). If it finds a mismatch, the fix is a follow-up, not a blocker.
- The raw-bytes fast path is only for identity hooks. T-0022 installs real hooks and parses.

## Log

- 2026-10-17 architect: created under epic T-0009; proposed pending T-0018 and decision D1.
- 2026-10-17 architect: D1 resolved (POSIX allowed in transpiler/src/platform/); D4 resolved (the human installs clangd; skip with 77 when absent). Wrote the full blueprint, fixed the Q7/Q8 behaviour by design so the spike is no longer a hard dependency (depends_on: [T-0019]), and moved it to ready.
