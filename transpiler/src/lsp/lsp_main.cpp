// asynctp-lsp: the editor launches this instead of clangd. Flags only; the
// proxying lives in proxy.cpp.

#include "asynctp/lsp/proxy.hpp"
#include "asynctp/platform/process.hpp"

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef ASYNCTP_VERSION
#define ASYNCTP_VERSION "unknown"
#endif

namespace {

void print_help() {
  std::cout << "usage: asynctp-lsp [--clangd=<path>] [--log=<file>] [--] [clangd args...]\n"
               "\n"
               "Language server for dotawait. Point your editor's clangd setting at this\n"
               "program: it starts the real clangd and relays LSP messages both ways.\n"
               "\n"
               "  --clangd=<path>  clangd to run. Default: $ASYNCTP_CLANGD, else the first\n"
               "                   `clangd` in PATH that is not asynctp-lsp itself, else\n"
               "                   the highest-numbered `clangd-NN` in PATH\n"
               "  --log=<file>     append one line per message (direction, method or id,\n"
               "                   size) to <file>; message bodies are never logged\n"
               "  --version        print clangd's --version output, then asynctp-lsp's\n"
               "  --help           print this help\n"
               "\n"
               "Any other argument, and everything after `--`, is forwarded to clangd\n"
               "(e.g. --background-index). clangd's own --log=<level> must go after `--`.\n";
}

// `clangd --version`, verbatim, then our own line. Editor extensions parse
// the first line for "clangd version X", so it has to come first.
int print_version(const std::filesystem::path& clangd) {
  auto spawned = asynctp::platform::spawn_with_pipes({clangd.string(), "--version"});
  if (auto* err = std::get_if<std::string>(&spawned)) {
    std::cerr << "asynctp-lsp: " << *err << "\n";
    return 1;
  }
  auto child = std::get<asynctp::platform::Child>(spawned);
  asynctp::platform::close_fd(child.in_fd);
  std::string out;
  {
    asynctp::platform::FdInStreambuf buf(child.out_fd);
    std::ostringstream ss;
    ss << &buf;
    out = ss.str();
  }
  asynctp::platform::close_fd(child.out_fd);
  int status = asynctp::platform::wait_child(child, std::chrono::seconds(5));
  std::cout << out;
  if (!out.empty() && out.back() != '\n') std::cout << "\n";
  std::cout << "asynctp-lsp " << ASYNCTP_VERSION << " (proxying " << clangd.string() << ")\n";
  if (status != 0) {
    std::cerr << "asynctp-lsp: " << clangd.string() << " --version exited with status " << status << "\n";
    return 1;
  }
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  // A dead clangd must surface as a failed write we can report, not kill us.
  std::signal(SIGPIPE, SIG_IGN);
  // Unsynced streams read/write fds directly: faster, and no C stdio lock is
  // held by a client reader that run_proxy leaves blocked after a crash.
  std::ios::sync_with_stdio(false);
  // cin is tied to cout by default, so every read in the client-reader
  // thread would flush cout outside client_mutex while the server pump is
  // writing it; the racing flushes emit buffered bytes twice.
  std::cin.tie(nullptr);

  std::optional<std::string> flag;
  std::optional<std::string> log_path;
  bool want_version = false;
  bool want_help = false;
  std::vector<std::string> forwarded;
  bool after_separator = false;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (after_separator) {
      forwarded.emplace_back(arg);
    } else if (arg == "--") {
      after_separator = true;
    } else if (arg.starts_with("--clangd=")) {
      flag = std::string(arg.substr(9));
    } else if (arg.starts_with("--log=")) {
      log_path = std::string(arg.substr(6));
    } else if (arg == "--version") {
      want_version = true;
    } else if (arg == "--help") {
      want_help = true;
    } else {
      // Unknown options go to clangd so users keep one argument list.
      forwarded.emplace_back(arg);
    }
  }

  if (want_help) {
    print_help();
    return 0;
  }

  const char* env = std::getenv("ASYNCTP_CLANGD");
  std::optional<std::string> env_value = env ? std::optional<std::string>(env) : std::nullopt;
  const char* path = std::getenv("PATH");
  auto clangd = asynctp::lsp::discover_clangd(flag, env_value, asynctp::lsp::split_path_list(path ? path : ""),
                                              asynctp::platform::self_executable());

  std::string not_found;
  if (!clangd) {
    if (flag && !flag->empty()) {
      not_found =
          "asynctp-lsp: --clangd=" + *flag + " resolves to asynctp-lsp itself; pass the path of the real clangd";
    } else if (env_value && !env_value->empty()) {
      not_found = "asynctp-lsp: ASYNCTP_CLANGD=" + *env_value +
                  " resolves to asynctp-lsp itself; set it to the path of the real clangd";
    } else {
      not_found = "asynctp-lsp: could not find clangd; pass --clangd=<path> or set ASYNCTP_CLANGD";
    }
  }

  if (want_version) {
    if (!clangd) {
      std::cerr << not_found << "\n";
      return 1;
    }
    return print_version(*clangd);
  }

  std::ofstream log_file;
  if (log_path) {
    log_file.open(*log_path, std::ios::app);
    if (!log_file) std::cerr << "asynctp-lsp: could not open log file " << *log_path << "; not logging\n";
  }
  std::ostream* log = log_file.is_open() ? &log_file : nullptr;

  if (!clangd) {
    // stderr ends up in the editor's server log; the LSP error goes on screen.
    std::cerr << not_found << "\n";
    return asynctp::lsp::run_without_clangd(not_found, std::cin, std::cout, log);
  }
  return asynctp::lsp::run_proxy(*clangd, forwarded, std::cin, std::cout, {}, log);
}
