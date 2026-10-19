#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <streambuf>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The child-process and file-descriptor layer for asynctp-lsp. Decision D1
// (T-0009): POSIX is allowed in transpiler/, but only in
// src/platform/process_posix.cpp. This header itself stays standard-library
// only, so nothing that includes it sees a POSIX header. Windows is a
// compile-time #error in the implementation file.

namespace asynctp::platform {

// A spawned child with pipes on its stdin and stdout; stderr is inherited
// (clangd logs there, and editors show our stderr as the server log).
// Plain fds rather than an RAII type: the proxy hands them to different
// threads and closes in_fd at a precise point to signal EOF to the child.
// Use close_fd()/wait_child() to release them.
struct Child {
  int pid = -1;
  int in_fd = -1;  // the child's stdin: we write
  int out_fd = -1; // the child's stdout: we read
};

// Starts argv[0] (looked up in PATH like execvp when it has no '/') with
// argv as its arguments. Returns the child, or a message. A failed exec is
// reported synchronously ("could not execute <path>: <reason>"), not as a
// child that exits 127, so callers can tell the user before talking to it.
// Every fd it creates is close-on-exec, so later children don't inherit
// them and an EOF really means the child is gone.
std::variant<Child, std::string> spawn_with_pipes(const std::vector<std::string>& argv);

// Waits for the child to exit and returns its exit code, or 128+signal if it
// was killed by a signal (shell convention). If it is still running after
// `timeout` it gets SIGTERM, then SIGKILL one second later. Returns -1 if
// there is no child to wait for. Sets child.pid to -1; does not close fds.
int wait_child(Child& child, std::chrono::milliseconds timeout);

// Closes fd if it is open and sets it to -1, so closing twice is harmless.
void close_fd(int& fd);

// Writes every byte, looping on partial writes and EINTR. Returns false on
// any other error, e.g. EPIPE once the reader is gone (main() ignores
// SIGPIPE so that surfaces here instead of killing the process).
bool write_all(int fd, std::string_view bytes);

// A read-only, buffered streambuf over a file descriptor, so the framing
// code (lsp::read_message takes a std::istream) can read a pipe. It does
// not own the fd. read(2) returning 0 or an error is end of stream.
class FdInStreambuf : public std::streambuf {
public:
  explicit FdInStreambuf(int fd, std::size_t buffer_size = 64 * 1024);

  FdInStreambuf(const FdInStreambuf&) = delete;
  FdInStreambuf& operator=(const FdInStreambuf&) = delete;

protected:
  int_type underflow() override;

private:
  int fd_;
  std::vector<char> buffer_;
};

// Absolute, symlink-free path of the running executable (Linux:
// /proc/self/exe; macOS: _NSGetExecutablePath), or nullopt if the OS won't
// say. clangd discovery uses it to avoid launching itself.
std::optional<std::filesystem::path> self_executable();

} // namespace asynctp::platform
