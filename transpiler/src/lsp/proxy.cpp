#include "asynctp/lsp/proxy.hpp"

#include "asynctp/lsp/framing.hpp"
#include "asynctp/lsp/json.hpp"
#include "asynctp/platform/process.hpp"

#include <charconv>
#include <condition_variable>
#include <istream>
#include <memory>
#include <mutex>
#include <ostream>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

namespace asynctp::lsp {

namespace fs = std::filesystem;

namespace {

bool is_executable_file(const fs::path& p) {
  std::error_code ec;
  auto st = fs::status(p, ec); // follows symlinks: a link to clangd counts
  if (ec || !fs::is_regular_file(st)) return false;
  constexpr auto any_exec = fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;
  return (st.permissions() & any_exec) != fs::perms::none;
}

fs::path canonical_or_normal(const fs::path& p) {
  std::error_code ec;
  fs::path c = fs::weakly_canonical(p, ec);
  return ec ? p.lexically_normal() : c;
}

// "clangd-NN" -> NN; nullopt for anything else (clangd-tidy, clangd-, ...).
std::optional<unsigned long> clangd_version_suffix(std::string_view name) {
  constexpr std::string_view prefix = "clangd-";
  if (!name.starts_with(prefix) || name.size() == prefix.size()) return std::nullopt;
  std::string_view digits = name.substr(prefix.size());
  unsigned long n = 0;
  auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), n);
  if (ec != std::errc{} || ptr != digits.data() + digits.size()) return std::nullopt;
  return n;
}

// What the proxy needs to know about a client message. Parsed with Json,
// never by substring search: `"method":"exit"` can legally appear inside
// a string in a didChange.
struct MessageInfo {
  std::string method;
  std::optional<Json> id;
};

MessageInfo inspect(const std::string& body) {
  MessageInfo info;
  auto parsed = Json::parse(body);
  auto* j = std::get_if<Json>(&parsed);
  if (!j) return info;
  if (const Json* m = j->find("method"); m && m->is_string()) info.method = std::string(m->as_string());
  if (const Json* id = j->find("id"); id && (id->is_int() || id->is_string())) info.id = *id;
  return info;
}

std::string label(const MessageInfo& info) {
  if (!info.method.empty()) return info.method;
  if (info.id) return "id=" + info.id->dump();
  return "?";
}

std::string error_response(const Json& id, std::string_view message) {
  Json err;
  err["code"] = -32603; // InternalError
  err["message"] = message;
  Json r;
  r["jsonrpc"] = "2.0";
  r["id"] = id;
  r["error"] = std::move(err);
  return r.dump();
}

std::string show_error_message(std::string_view message) {
  Json params;
  params["type"] = 1; // MessageType.Error
  params["message"] = message;
  Json n;
  n["jsonrpc"] = "2.0";
  n["method"] = "window/showMessage";
  n["params"] = std::move(params);
  return n.dump();
}

std::string frame(std::string_view body) {
  std::ostringstream out;
  write_message(out, body);
  return out.str();
}

// Everything the two pump threads share. Heap-allocated and co-owned by the
// client reader, because on the clangd-died path run_proxy returns while
// that thread is still blocked in client_in (see proxy.hpp).
struct Session {
  Hooks hooks; // a copy: the caller's may be gone before the reader is
  platform::Child child;
  std::ostream* log = nullptr;

  // Lock order: server_mutex or client_mutex, then state_mutex or
  // log_mutex. Never two of the outer ones at once.
  std::mutex server_mutex; // child.in_fd writes/close, `abandoned`
  std::mutex client_mutex; // client_out writes
  std::mutex log_mutex;
  std::mutex state_mutex; // everything below, plus `cv`
  std::condition_variable cv;

  bool abandoned = false; // run_proxy has returned; the reader must stop touching anything

  bool exit_forwarded = false;
  bool client_closed = false; // client reader finished and closed clangd's stdin
  bool server_closed = false;
  bool server_crashed = false; // clangd's stdout ended while the session was still live
  std::string server_error;    // framing error from clangd, if that is what ended it
  std::optional<Json> initialize_id;
  std::size_t server_messages = 0;

  void log_line(const std::string& line) {
    if (!log) return;
    std::lock_guard lk(log_mutex);
    *log << line << "\n";
    log->flush();
  }
};

void pump_client_to_server(const std::shared_ptr<Session>& s, std::istream& client_in) {
  std::string error;
  for (;;) {
    auto body = read_message(client_in, &error);
    if (!body) break;
    MessageInfo info = inspect(*body);
    bool is_exit = info.method == "exit";

    std::lock_guard lk(s->server_mutex);
    if (s->abandoned) return;
    s->log_line("--> " + label(info) + " " + std::to_string(body->size()));
    if (info.method == "initialize" && info.id) {
      std::lock_guard st(s->state_mutex);
      s->initialize_id = info.id;
    }
    std::optional<std::string> out =
        s->hooks.client_to_server ? s->hooks.client_to_server(std::move(*body)) : std::move(body);
    if (!out) {
      s->log_line("--> (swallowed by hook)");
      continue;
    }
    if (is_exit) {
      // Before the write: clangd may exit (and its stdout hit EOF) the
      // moment it reads this, and that EOF must not look like a crash.
      std::lock_guard st(s->state_mutex);
      s->exit_forwarded = true;
    }
    // A failed write means clangd is gone; the server pump sees the EOF and
    // reports it, so just keep draining the client until then.
    if (!platform::write_all(s->child.in_fd, frame(*out))) s->log_line("## write to clangd failed");
    if (is_exit) break;
  }

  std::lock_guard lk(s->server_mutex);
  if (s->abandoned) return;
  if (!error.empty()) s->log_line("## client stream error: " + error);
  {
    std::lock_guard st(s->state_mutex);
    s->client_closed = true;
  }
  // clangd's stdin EOF is its cue to shut down.
  platform::close_fd(s->child.in_fd);
  s->cv.notify_all();
}

void pump_server_to_client(Session& s, std::ostream& client_out) {
  platform::FdInStreambuf buf(s.child.out_fd);
  std::istream server_in(&buf);
  std::string error;
  for (;;) {
    auto body = read_message(server_in, &error);
    if (!body) break;
    {
      std::lock_guard st(s.state_mutex);
      ++s.server_messages;
    }
    // Only parse when someone needs it: identity forwarding is raw bytes.
    if (s.log) s.log_line("<-- " + label(inspect(*body)) + " " + std::to_string(body->size()));
    std::optional<std::string> out =
        s.hooks.server_to_client ? s.hooks.server_to_client(std::move(*body)) : std::move(body);
    if (!out) {
      s.log_line("<-- (swallowed by hook)");
      continue;
    }
    std::lock_guard lk(s.client_mutex);
    write_message(client_out, *out);
  }

  std::lock_guard st(s.state_mutex);
  s.server_closed = true;
  s.server_error = error;
  s.server_crashed = !s.exit_forwarded && !s.client_closed;
  s.cv.notify_all();
}

} // namespace

std::optional<fs::path> discover_clangd(std::optional<std::string> flag, std::optional<std::string> env_value,
                                        const std::vector<fs::path>& path_dirs,
                                        const std::optional<fs::path>& self) {
  std::optional<fs::path> self_canon;
  if (self) self_canon = canonical_or_normal(*self);
  auto is_self = [&](const fs::path& p) { return self_canon && canonical_or_normal(p) == *self_canon; };

  for (const auto* explicit_value : {&flag, &env_value}) {
    if (*explicit_value && !(*explicit_value)->empty()) {
      fs::path p(**explicit_value);
      if ((*explicit_value)->find('/') != std::string::npos) {
        if (is_self(p)) return std::nullopt;
        return p;
      }
      // execvp searches PATH for a name without '/' (the current directory
      // only via an empty PATH entry), so resolve it the same way and judge
      // what will actually run. Unresolved names go back as given, and
      // launching them reports "could not execute".
      for (const auto& dir : path_dirs) {
        fs::path candidate = dir / p;
        if (!is_executable_file(candidate)) continue;
        if (is_self(candidate)) return std::nullopt;
        return candidate;
      }
      return p;
    }
  }

  for (const auto& dir : path_dirs) {
    fs::path p = dir / "clangd";
    if (is_executable_file(p) && !is_self(p)) return p;
  }

  std::optional<fs::path> best;
  unsigned long best_version = 0;
  for (const auto& dir : path_dirs) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
      auto version = clangd_version_suffix(it->path().filename().string());
      if (!version || (best && *version <= best_version)) continue;
      if (!is_executable_file(it->path()) || is_self(it->path())) continue;
      best = it->path();
      best_version = *version;
    }
  }
  return best;
}

std::vector<fs::path> split_path_list(std::string_view path_value) {
  std::vector<fs::path> dirs;
  if (path_value.empty()) return dirs;
  for (;;) {
    std::size_t colon = path_value.find(':');
    std::string_view dir = path_value.substr(0, colon);
    dirs.emplace_back(dir.empty() ? std::string(".") : std::string(dir));
    if (colon == std::string_view::npos) break;
    path_value.remove_prefix(colon + 1);
  }
  return dirs;
}

int run_proxy(const fs::path& clangd, const std::vector<std::string>& clangd_args, std::istream& client_in,
              std::ostream& client_out, const Hooks& hooks, std::ostream* log) {
  std::vector<std::string> argv{clangd.string()};
  argv.insert(argv.end(), clangd_args.begin(), clangd_args.end());
  auto spawned = platform::spawn_with_pipes(argv);
  if (auto* err = std::get_if<std::string>(&spawned)) {
    return run_without_clangd("asynctp-lsp: " + *err + "; pass --clangd=<path> or set ASYNCTP_CLANGD", client_in,
                              client_out, log);
  }

  auto s = std::make_shared<Session>();
  s->hooks = hooks;
  s->child = std::get<platform::Child>(spawned);
  s->log = log;
  s->log_line("## started " + clangd.string() + " (pid " + std::to_string(s->child.pid) + ")");

  std::thread server_pump([s, &client_out] { pump_server_to_client(*s, client_out); });
  std::thread client_pump([s, &client_in] { pump_client_to_server(s, client_in); });

  bool crashed;
  {
    std::unique_lock st(s->state_mutex);
    s->cv.wait(st, [&] { return s->client_closed || s->server_crashed; });
    crashed = !s->client_closed;
  }

  if (!crashed) {
    int status = platform::wait_child(s->child, std::chrono::seconds(2));
    server_pump.join(); // clangd is gone, so its stdout is at EOF
    client_pump.join();
    platform::close_fd(s->child.out_fd);
    s->log_line("## clangd exited with status " + std::to_string(status));
    std::lock_guard st(s->state_mutex);
    return s->exit_forwarded ? status : 1;
  }

  // clangd died (or broke the framing) mid-session. Tell the editor, then
  // end the session so it sees the server go away rather than a silent one.
  int status = platform::wait_child(s->child, std::chrono::seconds(2));
  server_pump.join();
  platform::close_fd(s->child.out_fd);

  std::string message;
  std::optional<Json> pending_initialize;
  {
    std::lock_guard st(s->state_mutex);
    if (s->server_error.empty()) {
      message = "asynctp-lsp: clangd exited unexpectedly (status " + std::to_string(status) + "); see the clangd log";
    } else {
      message = "asynctp-lsp: clangd sent an invalid message (" + s->server_error + ") and was stopped (status " +
                std::to_string(status) + "); see the clangd log";
    }
    // Nothing came back at all, so the initialize can't have been answered;
    // fail it rather than leave the editor waiting forever.
    if (s->server_messages == 0) pending_initialize = s->initialize_id;
  }
  s->log_line("## " + message);
  {
    std::lock_guard lk(s->client_mutex);
    if (pending_initialize) write_message(client_out, error_response(*pending_initialize, message));
    write_message(client_out, show_error_message(message));
  }

  bool reader_finished;
  {
    std::lock_guard lk(s->server_mutex);
    s->abandoned = true;
    platform::close_fd(s->child.in_fd);
    std::lock_guard st(s->state_mutex);
    reader_finished = s->client_closed;
  }
  if (reader_finished) {
    client_pump.join();
  } else {
    client_pump.detach(); // blocked in client_in; it returns as soon as it wakes
  }
  return 1;
}

int run_without_clangd(std::string_view message, std::istream& client_in, std::ostream& client_out,
                       std::ostream* log) {
  auto log_line = [&](const std::string& line) {
    if (log) *log << line << "\n" << std::flush;
  };
  log_line("## " + std::string(message));
  bool shown = false;
  for (;;) {
    std::string error;
    auto body = read_message(client_in, &error);
    if (!body) {
      if (!error.empty()) log_line("## client stream error: " + error);
      return 1;
    }
    MessageInfo info = inspect(*body);
    log_line("--> " + label(info) + " " + std::to_string(body->size()));
    if (info.method == "exit") return 1;
    if (info.method.empty() || !info.id) continue; // notifications and client responses
    write_message(client_out, error_response(*info.id, message));
    if (!shown) {
      write_message(client_out, show_error_message(message));
      shown = true;
    }
  }
}

} // namespace asynctp::lsp
