#if !defined(__unix__) && !defined(__APPLE__)
#error "asynctp-lsp needs POSIX (see T-0009 D1)"
#endif

// The only translation unit in transpiler/ allowed to include POSIX headers
// (decision D1). Keep it that way: anything new that needs the OS goes here,
// behind a standard-library-only declaration in process.hpp.

#include "asynctp/platform/process.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <system_error>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace asynctp::platform {

namespace {

// pipe2(O_CLOEXEC) is Linux-only; pipe + fcntl works everywhere. The window
// in between doesn't matter: we only spawn before the proxy starts threads.
bool make_cloexec_pipe(int fds[2]) {
  if (::pipe(fds) != 0) return false;
  for (int i = 0; i < 2; ++i) ::fcntl(fds[i], F_SETFD, FD_CLOEXEC);
  return true;
}

int decode_status(int status) {
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return -1;
}

// Polls waitpid until the child is reaped or the deadline passes.
std::optional<int> reap_until(int pid, std::chrono::steady_clock::time_point deadline) {
  for (;;) {
    int status = 0;
    pid_t r = ::waitpid(pid, &status, WNOHANG);
    if (r == pid) return decode_status(status);
    if (r < 0 && errno != EINTR) return -1; // not our child (already reaped)
    if (std::chrono::steady_clock::now() >= deadline) return std::nullopt;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

} // namespace

std::variant<Child, std::string> spawn_with_pipes(const std::vector<std::string>& argv) {
  if (argv.empty()) return std::string("could not execute: empty command line");

  // Build argv before fork: after fork only async-signal-safe calls are
  // allowed in the child, so no allocation there.
  std::vector<char*> cargv;
  cargv.reserve(argv.size() + 1);
  for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
  cargv.push_back(nullptr);

  int to_child[2], from_child[2], exec_err[2];
  if (!make_cloexec_pipe(to_child)) return "could not create pipe: " + std::string(std::strerror(errno));
  if (!make_cloexec_pipe(from_child)) {
    int e = errno;
    ::close(to_child[0]);
    ::close(to_child[1]);
    return "could not create pipe: " + std::string(std::strerror(e));
  }
  // Self-pipe for exec failure: the child writes errno to it if execvp
  // returns; on success close-on-exec closes it and the parent reads EOF.
  if (!make_cloexec_pipe(exec_err)) {
    int e = errno;
    for (int fd : {to_child[0], to_child[1], from_child[0], from_child[1]}) ::close(fd);
    return "could not create pipe: " + std::string(std::strerror(e));
  }

  pid_t pid = ::fork();
  if (pid < 0) {
    int e = errno;
    for (int fd : {to_child[0], to_child[1], from_child[0], from_child[1], exec_err[0], exec_err[1]}) {
      ::close(fd);
    }
    return "could not fork: " + std::string(std::strerror(e));
  }

  if (pid == 0) {
    // Child. Move both ends above 2 first, so dup2 can't clobber one with
    // the other when the parent was started with stdin/stdout closed.
    int in = ::fcntl(to_child[0], F_DUPFD, 3);
    int out = ::fcntl(from_child[1], F_DUPFD, 3);
    if (in < 0 || out < 0 || ::dup2(in, 0) < 0 || ::dup2(out, 1) < 0) {
      int e = errno;
      (void)!::write(exec_err[1], &e, sizeof e);
      ::_exit(127);
    }
    ::close(in);
    ::close(out);
    ::execvp(cargv[0], cargv.data());
    int e = errno;
    (void)!::write(exec_err[1], &e, sizeof e);
    ::_exit(127);
  }

  ::close(to_child[0]);
  ::close(from_child[1]);
  ::close(exec_err[1]);

  int child_errno = 0;
  ssize_t n;
  do {
    n = ::read(exec_err[0], &child_errno, sizeof child_errno);
  } while (n < 0 && errno == EINTR);
  ::close(exec_err[0]);

  if (n > 0) {
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    ::close(to_child[1]);
    ::close(from_child[0]);
    return "could not execute " + argv[0] + ": " + std::strerror(child_errno);
  }

  Child child;
  child.pid = pid;
  child.in_fd = to_child[1];
  child.out_fd = from_child[0];
  return child;
}

int wait_child(Child& child, std::chrono::milliseconds timeout) {
  if (child.pid < 0) return -1;
  int pid = child.pid;
  child.pid = -1;

  auto now = std::chrono::steady_clock::now();
  if (auto code = reap_until(pid, now + timeout)) return *code;
  ::kill(pid, SIGTERM);
  if (auto code = reap_until(pid, std::chrono::steady_clock::now() + std::chrono::seconds(1))) {
    return *code;
  }
  ::kill(pid, SIGKILL);
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) return -1;
  }
  return decode_status(status);
}

void close_fd(int& fd) {
  if (fd >= 0) {
    // No EINTR retry: on Linux the fd is released even when close reports
    // EINTR, so retrying could close an fd another thread just opened.
    ::close(fd);
    fd = -1;
  }
}

bool write_all(int fd, std::string_view bytes) {
  if (fd < 0) return false;
  while (!bytes.empty()) {
    ssize_t n = ::write(fd, bytes.data(), bytes.size());
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    bytes.remove_prefix(static_cast<std::size_t>(n));
  }
  return true;
}

FdInStreambuf::FdInStreambuf(int fd, std::size_t buffer_size) : fd_(fd), buffer_(buffer_size ? buffer_size : 1) {
  setg(buffer_.data(), buffer_.data(), buffer_.data());
}

FdInStreambuf::int_type FdInStreambuf::underflow() {
  if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
  if (fd_ < 0) return traits_type::eof();
  ssize_t n;
  do {
    n = ::read(fd_, buffer_.data(), buffer_.size());
  } while (n < 0 && errno == EINTR);
  if (n <= 0) return traits_type::eof();
  setg(buffer_.data(), buffer_.data(), buffer_.data() + n);
  return traits_type::to_int_type(*gptr());
}

std::optional<std::filesystem::path> self_executable() {
  std::error_code ec;
#if defined(__APPLE__)
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buf(size, '\0');
  if (_NSGetExecutablePath(buf.data(), &size) != 0) return std::nullopt;
  buf.resize(std::strlen(buf.c_str()));
  auto p = std::filesystem::canonical(buf, ec);
#else
  auto p = std::filesystem::canonical("/proc/self/exe", ec);
#endif
  if (ec) return std::nullopt;
  return p;
}

} // namespace asynctp::platform
