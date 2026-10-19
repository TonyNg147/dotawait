#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asynctp::lsp {

// Finds the real clangd for asynctp-lsp. Pure apart from reading file
// status, so it is unit-tested with temp dirs. In order:
//   1. `flag` (--clangd=<path>);
//   2. `env_value` (ASYNCTP_CLANGD);
//   3. the first executable regular file named `clangd` in `path_dirs`
//      order whose weakly_canonical path is not `self`. This skips the
//      user's own `clangd -> asynctp-lsp` symlink, which would otherwise
//      make us spawn ourselves forever;
//   4. the highest-numbered executable `clangd-NN` (numeric compare, so
//      clangd-21 beats clangd-9) in any dir, again skipping `self`; on a
//      tie the earlier dir wins.
// An empty flag/env value counts as unset. A flag or env value that
// resolves to `self` is an error (nullopt, no fallback), since the user
// asked for something that can't work. nullopt also means nothing found.
// A flag/env value containing '/' is returned as given. A bare name
// (`clangd`) is resolved through `path_dirs` like execvp would, and the
// first executable match is returned, so `ASYNCTP_CLANGD=clangd` with the
// user's link first in PATH is caught as self instead of relaunching us
// (unbounded, since the child inherits the env var). Flag/env values are
// not checked for existence: launching a missing one reports a precise
// "could not execute" instead.
std::optional<std::filesystem::path> discover_clangd(
    std::optional<std::string> flag, std::optional<std::string> env_value,
    const std::vector<std::filesystem::path>& path_dirs, const std::optional<std::filesystem::path>& self);

// Splits a PATH value on ':' for discover_clangd. An empty entry (leading,
// trailing or doubled ':') means the current directory, as in POSIX sh; an
// empty value means no directories.
std::vector<std::filesystem::path> split_path_list(std::string_view path_value);

// The seam later tickets (T-0022..T-0024) use to rewrite traffic. Each hook
// gets one message body and returns the body to forward, or nullopt to
// swallow it. An empty std::function is the identity, and when
// server_to_client is empty (and no log is requested) server bodies are
// forwarded as raw bytes without being parsed at all.
struct Hooks {
  std::function<std::optional<std::string>(std::string&& body)> client_to_server, server_to_client;
};

// Spawns `clangd clangd_args...` and pumps LSP messages between it and the
// client (the editor) until the session ends:
//   - Client EOF, or `exit` forwarded: closes clangd's stdin, waits up to 2s
//     for it (then SIGTERM), and returns clangd's exit code if `exit` was
//     forwarded, else 1 (exit without the shutdown/exit handshake).
//   - clangd's stdout hits EOF before `exit` was forwarded: the client gets
//     a window/showMessage error naming the exit status (and, if clangd had
//     not sent anything yet, an error response to its `initialize`), and
//     run_proxy returns 1 without waiting for the client.
//   - clangd can't be spawned: behaves like run_without_clangd() with a
//     "could not execute" message.
// Client bodies are always parsed (lsp::Json) to spot `exit` and remember
// the `initialize` id; server bodies only when a hook or log needs them.
//
// Threads: the calling thread waits while one thread reads client_in and
// writes clangd's stdin, and another reads clangd's stdout and writes
// client_out. On the clangd-died path the client reader may still be
// blocked inside client_in when this returns, so it is detached; from then
// on it touches nothing but client_in and its own state. client_in must
// therefore outlive the process in that case (std::cin does; asynctp-lsp
// then just returns from main). client_in must not be tie()d to
// client_out: a tied read flushes client_out outside the writer's lock,
// and racing flushes duplicate bytes (asynctp-lsp unties std::cin).
//
// `log`, if set, gets one line per message, `<direction> <method or id>
// <bytes>` (bodies are never logged), plus lifecycle notes.
int run_proxy(const std::filesystem::path& clangd, const std::vector<std::string>& clangd_args,
              std::istream& client_in, std::ostream& client_out, const Hooks& hooks, std::ostream* log);

// The fail-loudly mode for when there is no clangd to talk to: answers every
// client request (including `initialize`) with JSON-RPC error -32603
// carrying `message`, sends `message` once as a window/showMessage error
// after the first answer, ignores notifications, and returns 1 at `exit` or
// client EOF.
int run_without_clangd(std::string_view message, std::istream& client_in, std::ostream& client_out,
                       std::ostream* log);

} // namespace asynctp::lsp
