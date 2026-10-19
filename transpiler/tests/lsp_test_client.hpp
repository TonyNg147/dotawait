#pragma once

// Shared by lsp_proxy_tests and lsp_clangd_smoke: drives a spawned
// asynctp-lsp as the editor would, over its stdin/stdout. Goes through
// asynctp::platform so test code stays free of POSIX headers too.

#include "asynctp/lsp/framing.hpp"
#include "asynctp/platform/process.hpp"

#include <chrono>
#include <istream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace asynctp::test {

inline std::string frame(std::string_view body) {
  std::ostringstream out;
  asynctp::lsp::write_message(out, body);
  return out.str();
}

class LspClient {
public:
  // Spawns argv; check started() / spawn_error() before use.
  explicit LspClient(const std::vector<std::string>& argv) {
    auto r = asynctp::platform::spawn_with_pipes(argv);
    if (auto* err = std::get_if<std::string>(&r)) {
      spawn_error_ = *err;
      return;
    }
    child_ = std::get<asynctp::platform::Child>(r);
    buf_ = std::make_unique<asynctp::platform::FdInStreambuf>(child_.out_fd);
    in_ = std::make_unique<std::istream>(buf_.get());
  }

  LspClient(const LspClient&) = delete;
  LspClient& operator=(const LspClient&) = delete;

  ~LspClient() {
    close_input();
    if (child_.pid >= 0) asynctp::platform::wait_child(child_, std::chrono::milliseconds(0));
    asynctp::platform::close_fd(child_.out_fd);
  }

  bool started() const { return child_.pid >= 0 || exited_; }
  const std::string& spawn_error() const { return spawn_error_; }

  bool send(std::string_view body) { return asynctp::platform::write_all(child_.in_fd, frame(body)); }
  bool send_raw(std::string_view bytes) { return asynctp::platform::write_all(child_.in_fd, bytes); }

  // Next message body; nullopt at EOF or on a framing error.
  std::optional<std::string> recv() {
    if (!in_) return std::nullopt;
    std::string error;
    return asynctp::lsp::read_message(*in_, &error);
  }

  // Everything the child writes until it closes stdout, unframed.
  std::string read_all() {
    if (!in_) return {};
    std::ostringstream out;
    out << in_->rdbuf();
    return out.str();
  }

  void close_input() { asynctp::platform::close_fd(child_.in_fd); }

  int wait(std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
    exited_ = true;
    return asynctp::platform::wait_child(child_, timeout);
  }

private:
  asynctp::platform::Child child_;
  std::unique_ptr<asynctp::platform::FdInStreambuf> buf_;
  std::unique_ptr<std::istream> in_;
  std::string spawn_error_;
  bool exited_ = false;
};

} // namespace asynctp::test
