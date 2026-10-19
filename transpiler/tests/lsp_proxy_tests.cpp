// asynctp-lsp as a transparent clangd proxy (T-0021).
//
// Unit part: discover_clangd against temp dirs of fake executables.
// Process part: the real asynctp-lsp binary, pointed at fake_lsp_server,
// driven over pipes the way an editor drives it. Plus in-process run_proxy
// calls for the hook seam, which the binary doesn't expose.
//
// Usage: lsp_proxy_tests <path-to-asynctp-lsp> <path-to-fake_lsp_server>

#include "asynctp/lsp/json.hpp"
#include "asynctp/lsp/proxy.hpp"
#include "lsp_test_client.hpp"
#include "test_support.hpp"

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using asynctp::lsp::discover_clangd;
using asynctp::lsp::Json;
using asynctp::test::LspClient;

namespace {

std::string g_lsp;  // asynctp-lsp
std::string g_fake; // fake_lsp_server

class TempDir {
public:
  TempDir() {
    std::random_device rd;
    path_ = fs::temp_directory_path() / ("asynctp_lsp_tests_" + std::to_string(rd()) + std::to_string(rd()));
    fs::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  const fs::path& path() const { return path_; }
  fs::path operator/(const std::string& s) const { return path_ / s; }

private:
  fs::path path_;
};

fs::path make_file(const fs::path& p, bool executable = true) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << "#!/bin/sh\n";
  auto perms = fs::perms::owner_read | fs::perms::owner_write;
  if (executable) perms |= fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;
  fs::permissions(p, perms);
  return p;
}

std::string read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool contains(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

Json parse(const std::string& body) {
  auto r = Json::parse(body);
  if (auto* j = std::get_if<Json>(&r)) return *j;
  return Json();
}

// --- discover_clangd -------------------------------------------------------

void test_discover_precedence() {
  TempDir t;
  fs::path a = make_file(t / "a/clangd");
  fs::path b = make_file(t / "b/clangd-21");
  std::vector<fs::path> dirs = {t / "a", t / "b"};
  std::optional<fs::path> self = make_file(t / "self/asynctp-lsp");

  CHECK_EQ(discover_clangd("/opt/flag/clangd", "/opt/env/clangd", dirs, self), fs::path("/opt/flag/clangd"));
  CHECK_EQ(discover_clangd(std::nullopt, "/opt/env/clangd", dirs, self), fs::path("/opt/env/clangd"));
  CHECK_EQ(discover_clangd(std::nullopt, std::nullopt, dirs, self), a);
  CHECK_EQ(discover_clangd(std::nullopt, std::nullopt, {t / "b", t / "a"}, self), a); // plain beats -NN anywhere
  CHECK_EQ(discover_clangd(std::nullopt, std::nullopt, {t / "b"}, self), b);
  // Empty values count as unset.
  CHECK_EQ(discover_clangd("", "", dirs, self), a);
}

void test_discover_path_order_and_executable_bit() {
  TempDir t;
  make_file(t / "first/clangd", /*executable=*/false);
  fs::create_directories(t / "dir/clangd"); // a directory named clangd
  fs::path good = make_file(t / "second/clangd");
  std::vector<fs::path> dirs = {t / "missing", t / "first", t / "dir", t / "second"};
  CHECK_EQ(discover_clangd(std::nullopt, std::nullopt, dirs, std::nullopt), good);
}

void test_discover_skips_path_clangd_that_is_self() {
  TempDir t;
  fs::path self = make_file(t / "bin/asynctp-lsp");
  fs::create_directories(t / "links");
  fs::create_symlink(self, t / "links/clangd"); // the user's `clangd -> asynctp-lsp`
  fs::path real = make_file(t / "usr/clangd");

  CHECK_EQ(discover_clangd(std::nullopt, std::nullopt, {t / "links", t / "usr"}, self), real);

  // With only the self link in PATH, fall through to clangd-NN.
  fs::path numbered = make_file(t / "usr2/clangd-18");
  CHECK_EQ(discover_clangd(std::nullopt, std::nullopt, {t / "links", t / "usr2"}, self), numbered);

  // A clangd-NN that is self is skipped too.
  fs::create_symlink(self, t / "links/clangd-99");
  CHECK_EQ(discover_clangd(std::nullopt, std::nullopt, {t / "links", t / "usr2"}, self), numbered);
  CHECK(!discover_clangd(std::nullopt, std::nullopt, {t / "links"}, self).has_value());
}

void test_discover_flag_or_env_at_self_is_error() {
  TempDir t;
  fs::path self = make_file(t / "bin/asynctp-lsp");
  fs::create_directories(t / "links");
  fs::create_symlink(self, t / "links/clangd");
  make_file(t / "usr/clangd");
  std::vector<fs::path> dirs = {t / "usr"};

  CHECK(!discover_clangd(self.string(), std::nullopt, dirs, self).has_value());
  CHECK(!discover_clangd((t / "links/clangd").string(), std::nullopt, dirs, self).has_value());
  // Non-canonical spelling of self.
  CHECK(!discover_clangd((t / "bin/../bin/asynctp-lsp").string(), std::nullopt, dirs, self).has_value());
  CHECK(!discover_clangd(std::nullopt, self.string(), dirs, self).has_value());
}

void test_discover_bare_flag_resolves_like_execvp() {
  TempDir t;
  fs::path self = make_file(t / "bin/asynctp-lsp");
  make_file(t / "a/my-clangd", false); // not executable: execvp skips it too
  fs::path real = make_file(t / "b/my-clangd");
  std::vector<fs::path> dirs = {t / "a", t / "b"};
  CHECK_EQ(discover_clangd("my-clangd", std::nullopt, dirs, self), real);
  CHECK_EQ(discover_clangd(std::nullopt, "my-clangd", dirs, self), real);
  // Not in PATH: handed back as given, so the launch reports "could not execute".
  CHECK_EQ(discover_clangd("no-such-clangd", std::nullopt, dirs, self), fs::path("no-such-clangd"));
}

void test_discover_highest_numbered() {
  TempDir t;
  make_file(t / "a/clangd-14");
  make_file(t / "a/clangd-9");      // numerically lower than 21, lexically higher
  make_file(t / "a/clangd-tidy");   // not a version
  make_file(t / "a/clangd-");       // no digits
  make_file(t / "a/clangd-30", false); // not executable
  fs::path b21 = make_file(t / "b/clangd-21");
  make_file(t / "c/clangd-21"); // tie: earlier dir wins
  CHECK_EQ(discover_clangd(std::nullopt, std::nullopt, {t / "a", t / "b", t / "c"}, std::nullopt), b21);
}

void test_discover_nothing_found() {
  TempDir t;
  make_file(t / "a/clang");
  make_file(t / "a/clangd.sh");
  CHECK(!discover_clangd(std::nullopt, std::nullopt, {}, std::nullopt).has_value());
  CHECK(!discover_clangd(std::nullopt, std::nullopt, {t / "a", t / "nope"}, std::nullopt).has_value());
}

void test_split_path_list() {
  using asynctp::lsp::split_path_list;
  CHECK(split_path_list("").empty());
  CHECK(split_path_list("/usr/bin:/bin") == std::vector<fs::path>({"/usr/bin", "/bin"}));
  CHECK(split_path_list(":/bin::/x:") == std::vector<fs::path>({".", "/bin", ".", "/x", "."}));
}

// --- the asynctp-lsp binary ------------------------------------------------

// Bodies with deliberately non-canonical spacing and key order: a proxy
// that re-serialised JSON would change them, so the transcript comparison
// proves the bytes went through untouched.
const std::string kInit = R"({ "jsonrpc":"2.0", "id" : 1, "method":"initialize", "params":{"processId":null, "rootUri":null, "capabilities":{}} })";
const std::string kEcho = R"({"method":"fake/echo","jsonrpc":"2.0","params":{"x":[1,"two",null],"u":"é"}})";
const std::string kShutdown = R"({"jsonrpc":"2.0","id":"s-2","method":"shutdown"})";
const std::string kExit = R"({"jsonrpc":"2.0","method":"exit"})";

void test_session_round_trip() {
  TempDir t;
  fs::path transcript = t / "transcript.txt";
  ::setenv("FAKE_LSP_TRANSCRIPT", transcript.c_str(), 1);

  LspClient c({g_lsp, "--clangd=" + g_fake});
  CHECK(c.started());
  if (!c.started()) return;

  CHECK(c.send(kInit));
  CHECK_EQ(c.recv().value_or("<eof>"), R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{}}})");
  CHECK(c.send(kEcho));
  CHECK_EQ(c.recv().value_or("<eof>"),
           "{\"jsonrpc\":\"2.0\",\"method\":\"fake/echoed\",\"params\":{\"x\":[1,\"two\",null],\"u\":\"\xc3\xa9\"}}");
  CHECK(c.send(kShutdown));
  CHECK_EQ(c.recv().value_or("<eof>"), R"({"jsonrpc":"2.0","id":"s-2","result":null})");
  CHECK(c.send(kExit));
  CHECK_EQ(c.recv().value_or("<eof>"), "<eof>");
  CHECK_EQ(c.wait(), 0);

  CHECK_EQ(read_file(transcript), kInit + "\n" + kEcho + "\n" + kShutdown + "\n" + kExit + "\n");
  ::unsetenv("FAKE_LSP_TRANSCRIPT");
}

void test_message_split_across_writes() {
  // Pipes deliver arbitrary chunks; framing must not assume one read per
  // message, in either direction.
  LspClient c({g_lsp, "--clangd=" + g_fake});
  std::string framed = asynctp::test::frame(kInit) + asynctp::test::frame(kShutdown);
  for (std::size_t i = 0; i < framed.size(); i += 7) CHECK(c.send_raw(framed.substr(i, 7)));
  CHECK_EQ(c.recv().value_or("<eof>"), R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{}}})");
  CHECK_EQ(c.recv().value_or("<eof>"), R"({"jsonrpc":"2.0","id":"s-2","result":null})");
  CHECK(c.send(kExit));
  CHECK_EQ(c.wait(), 0);
}

void test_client_eof_without_exit_returns_1() {
  LspClient c({g_lsp, "--clangd=" + g_fake});
  CHECK(c.send(kInit));
  CHECK(c.recv().has_value());
  c.close_input();
  CHECK_EQ(c.recv().value_or("<eof>"), "<eof>");
  CHECK_EQ(c.wait(), 1);
}

void test_clangd_crash_is_reported() {
  LspClient c({g_lsp, "--clangd=" + g_fake});
  CHECK(c.send(kInit));
  CHECK(c.recv().has_value());
  CHECK(c.send(R"({"jsonrpc":"2.0","method":"fake/crash"})"));

  Json msg = parse(c.recv().value_or("null"));
  CHECK_EQ(msg.find("method") ? msg.find("method")->as_string() : "", "window/showMessage");
  const Json* params = msg.find("params");
  CHECK(params && params->find("type") && params->find("type")->as_int() == 1);
  std::string text(params && params->find("message") ? params->find("message")->as_string() : "");
  CHECK(contains(text, "clangd exited unexpectedly"));
  CHECK(contains(text, "status 3"));

  // The proxy exits on its own, without the client closing its stdin:
  // otherwise the editor would keep talking to a dead server.
  CHECK_EQ(c.recv().value_or("<eof>"), "<eof>");
  CHECK_EQ(c.wait(), 1);
}

void test_crash_before_any_reply_fails_initialize() {
  // clangd dies before answering initialize: the editor's pending request
  // gets an error instead of hanging forever. A shell that exits after
  // reading the first header line makes the order deterministic: it can
  // only die once the proxy has read (and remembered) the initialize.
  LspClient c({g_lsp, "--clangd=/bin/sh", "--", "-c", "read line; exit 5"});
  CHECK(c.send(kInit));

  Json resp = parse(c.recv().value_or("null"));
  CHECK(resp.find("id") && resp.find("id")->as_int() == 1);
  const Json* err = resp.find("error");
  CHECK(err && err->find("code") && err->find("code")->as_int() == -32603);
  std::string text(err && err->find("message") ? err->find("message")->as_string() : "");
  CHECK(contains(text, "clangd exited unexpectedly (status 5)"));

  Json show = parse(c.recv().value_or("null"));
  CHECK_EQ(show.find("method") ? show.find("method")->as_string() : "", "window/showMessage");
  const Json* params = show.find("params");
  CHECK(params && params->find("message") && params->find("message")->as_string() == text);

  CHECK_EQ(c.recv().value_or("<eof>"), "<eof>");
  CHECK_EQ(c.wait(), 1);
}

void check_unavailable_session(LspClient& c, std::string_view expect_any_of_a, std::string_view expect_any_of_b) {
  CHECK(c.send(kInit));
  Json resp = parse(c.recv().value_or("null"));
  CHECK(resp.find("id") && resp.find("id")->as_int() == 1);
  const Json* err = resp.find("error");
  CHECK(err && err->find("code") && err->find("code")->as_int() == -32603);
  std::string text(err && err->find("message") ? err->find("message")->as_string() : "");
  CHECK(contains(text, expect_any_of_a) || contains(text, expect_any_of_b));

  Json show = parse(c.recv().value_or("null"));
  CHECK_EQ(show.find("method") ? show.find("method")->as_string() : "", "window/showMessage");
  const Json* params = show.find("params");
  CHECK(params && params->find("type") && params->find("type")->as_int() == 1);
  CHECK(params && params->find("message") && params->find("message")->as_string() == text);

  // Every later request gets the same error; notifications get nothing.
  CHECK(c.send(R"({"jsonrpc":"2.0","method":"initialized","params":{}})"));
  CHECK(c.send(kShutdown));
  Json shut = parse(c.recv().value_or("null"));
  CHECK(shut.find("id") && shut.find("id")->as_string() == "s-2");
  CHECK(shut.find("error") != nullptr);
  CHECK(c.send(kExit));
  CHECK_EQ(c.recv().value_or("<eof>"), "<eof>");
  CHECK_EQ(c.wait(), 1);
}

void test_nonexistent_clangd_fails_loudly() {
  LspClient c({g_lsp, "--clangd=/nonexistent/clangd"});
  check_unavailable_session(c, "could not find clangd", "could not execute");
}

void test_clangd_not_found_fails_loudly() {
  TempDir t;
  std::string old_path = std::getenv("PATH") ? std::getenv("PATH") : "";
  const char* old_env = std::getenv("ASYNCTP_CLANGD");
  std::string saved_env = old_env ? old_env : "";
  ::setenv("PATH", t.path().c_str(), 1);
  ::unsetenv("ASYNCTP_CLANGD");

  {
    LspClient c({g_lsp});
    check_unavailable_session(c, "could not find clangd", "could not find clangd");
  }
  {
    LspClient c({g_lsp, "--version"});
    c.close_input();
    c.read_all();
    CHECK_EQ(c.wait(), 1);
  }

  ::setenv("PATH", old_path.c_str(), 1);
  if (old_env) ::setenv("ASYNCTP_CLANGD", saved_env.c_str(), 1);
}

void test_env_var_selects_clangd() {
  ::setenv("ASYNCTP_CLANGD", g_fake.c_str(), 1);
  LspClient c({g_lsp, "--version"});
  std::string out = c.read_all();
  CHECK_EQ(c.wait(), 0);
  CHECK(out.starts_with("clangd version 99.0.0 (fake)"));
  ::unsetenv("ASYNCTP_CLANGD");
}

void test_flag_at_self_fails_loudly() {
  LspClient c({g_lsp, "--clangd=" + g_lsp});
  check_unavailable_session(c, "asynctp-lsp itself", "asynctp-lsp itself");
}

void test_version() {
  LspClient c({g_lsp, "--clangd=" + g_fake, "--version"});
  std::string out = c.read_all();
  CHECK_EQ(c.wait(), 0);
  CHECK(out.starts_with("clangd version 99.0.0 (fake)\n"));
  CHECK(contains(out, "asynctp-lsp "));
  CHECK(contains(out, "(proxying " + g_fake + ")"));
}

void test_help() {
  LspClient c({g_lsp, "--help"});
  std::string out = c.read_all();
  CHECK_EQ(c.wait(), 0);
  CHECK(contains(out, "--clangd=<path>"));
  CHECK(contains(out, "forwarded to clangd"));
}

void test_unknown_options_are_forwarded() {
  TempDir t;
  fs::path argv_file = t / "argv.txt";
  ::setenv("FAKE_LSP_ARGV", argv_file.c_str(), 1);
  LspClient c({g_lsp, "--background-index", "--clangd=" + g_fake, "-j=4", "--", "--clangd=x", "--help"});
  CHECK(c.send(kExit));
  CHECK_EQ(c.wait(), 0);
  CHECK_EQ(read_file(argv_file), "--background-index\n-j=4\n--clangd=x\n--help\n");
  ::unsetenv("FAKE_LSP_ARGV");
}

// --- QA adversarial cases ----------------------------------------------------

void test_qa_discover_bare_name_that_execvp_resolves_to_self() {
  // execvp() searches PATH for a value without '/', so ASYNCTP_CLANGD=clangd
  // (or --clangd=clangd) with the user's `clangd -> asynctp-lsp` link first
  // in PATH spawns asynctp-lsp itself. Via the env var (inherited by the
  // child) that recurses without bound. discover_clangd must either reject
  // it or hand back a path that is not self.
  TempDir t;
  fs::path self = make_file(t / "bin/asynctp-lsp");
  fs::create_directories(t / "links");
  fs::create_symlink(self, t / "links/clangd");
  make_file(t / "usr/clangd");
  std::vector<fs::path> dirs = {t / "links", t / "usr"};
  auto not_self = [&](const std::optional<fs::path>& r) {
    if (!r) return true; // reported as an error: fine
    if (!r->has_parent_path()) return false; // bare name: execvp picks links/clangd == self
    return fs::weakly_canonical(*r) != fs::weakly_canonical(self);
  };
  CHECK(not_self(discover_clangd(std::nullopt, std::string("clangd"), dirs, self)));
  CHECK(not_self(discover_clangd(std::string("clangd"), std::nullopt, dirs, self)));
}

void test_qa_exit_inside_string_is_not_exit() {
  // "method":"exit" as text in a didChange must not end the session.
  LspClient c({g_lsp, "--clangd=" + g_fake});
  CHECK(c.send(kInit));
  CHECK(c.recv().has_value());
  CHECK(c.send(R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"text":"{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}"}})"));
  CHECK(c.send(R"({"jsonrpc":"2.0","method":"fake/echo","params":"still-alive"})"));
  CHECK_EQ(c.recv().value_or("<eof>"), R"({"jsonrpc":"2.0","method":"fake/echoed","params":"still-alive"})");
  CHECK(c.send(kShutdown));
  CHECK(c.recv().has_value());
  CHECK(c.send(kExit));
  CHECK_EQ(c.wait(), 0);
}

void test_qa_large_message_both_directions() {
  // 1 MiB each way: many FdInStreambuf refills and partial pipe writes.
  std::string big(1 << 20, 'x');
  for (std::size_t i = 0; i < big.size(); i += 97) big[i] = static_cast<char>('a' + (i / 97) % 26);
  LspClient c({g_lsp, "--clangd=" + g_fake});
  CHECK(c.send(kInit));
  CHECK(c.recv().has_value());
  CHECK(c.send(R"({"jsonrpc":"2.0","method":"fake/echo","params":")" + big + R"("})"));
  CHECK_EQ(c.recv().value_or("<eof>"), R"({"jsonrpc":"2.0","method":"fake/echoed","params":")" + big + R"("})");
  CHECK(c.send(kShutdown));
  CHECK(c.recv().has_value());
  CHECK(c.send(kExit));
  CHECK_EQ(c.wait(), 0);
}

void qa_pipelined(const std::vector<std::string>& argv) {
  LspClient c(argv);
  CHECK(c.send(kInit));
  for (int i = 0; i < 100; ++i) {
    CHECK(c.send(R"({"jsonrpc":"2.0","method":"fake/echo","params":)" + std::to_string(i) + "}"));
  }
  CHECK_EQ(c.recv().value_or("<eof>"), R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{}}})");
  for (int i = 0; i < 100; ++i) {
    std::string got = c.recv().value_or("<eof>");
    std::string want = R"({"jsonrpc":"2.0","method":"fake/echoed","params":)" + std::to_string(i) + "}";
    if (got != want) std::cerr << "QA pipelined " << argv[0] << " #" << i << " got: " << got.substr(0, 200) << "\n";
    CHECK_EQ(got, want);
    if (got == "<eof>") break;
  }
  CHECK(c.send(kShutdown));
  CHECK(c.recv().has_value());
  CHECK(c.send(kExit));
  CHECK_EQ(c.wait(), 0);
}

void test_qa_pipelined_messages_keep_order() {
  // Editors pipeline requests, so the client reader and the server writer
  // are busy at the same time. Every server message must arrive exactly
  // once, in order. Run against the fake directly first, so a failure only
  // in the second call is the proxy's (seen: duplicated messages, e.g. the
  // initialize result twice; std::cin is tied to std::cout, so the client
  // reader thread flushes cout outside client_mutex).
  qa_pipelined({g_fake});
  qa_pipelined({g_lsp, "--clangd=" + g_fake});
}

void test_qa_client_eof_mid_frame() {
  // The editor dies halfway through a message: no hang, exit 1.
  LspClient c({g_lsp, "--clangd=" + g_fake});
  CHECK(c.send(kInit));
  CHECK(c.recv().has_value());
  CHECK(c.send_raw("Content-Length: 100\r\n\r\n{\"jsonrpc\""));
  c.close_input();
  CHECK_EQ(c.recv().value_or("<eof>"), "<eof>");
  CHECK_EQ(c.wait(), 1);
}

void test_qa_server_garbage_is_reported() {
  // clangd writing non-LSP bytes to stdout must reach the editor as an error.
  LspClient c({g_lsp, "--clangd=/bin/sh", "--", "-c", "echo not-a-header; exec sleep 30"});
  CHECK(c.send(kInit));
  std::optional<std::string> got;
  bool saw_show = false;
  while ((got = c.recv())) {
    Json m = parse(*got);
    if (m.find("method") && m.find("method")->as_string() == "window/showMessage") {
      const Json* params = m.find("params");
      saw_show = params && params->find("message") &&
                 contains(params->find("message")->as_string(), "invalid message");
    }
  }
  CHECK(saw_show);
  CHECK_EQ(c.wait(), 1);
}

void test_qa_server_ignores_stdin_eof() {
  // clangd that never exits after `exit`: SIGTERM after the 2s grace, no hang.
  LspClient c({g_lsp, "--clangd=/bin/sh", "--", "-c", "exec sleep 30"});
  CHECK(c.send(kExit));
  CHECK_EQ(c.recv().value_or("<eof>"), "<eof>");
  CHECK(c.wait() != 0);
}

void test_qa_client_keeps_writing_after_crash() {
  // After the crash report the proxy exits even though the editor is still
  // sending; the editor's writes may then fail, which must not matter.
  LspClient c({g_lsp, "--clangd=" + g_fake});
  CHECK(c.send(kInit));
  CHECK(c.recv().has_value());
  CHECK(c.send(R"({"jsonrpc":"2.0","method":"fake/crash"})"));
  for (int i = 0; i < 20; ++i) (void)c.send(R"({"jsonrpc":"2.0","method":"fake/echo","params":1})");
  bool saw_crash = false;
  while (auto got = c.recv()) saw_crash = saw_crash || contains(*got, "clangd exited unexpectedly");
  CHECK(saw_crash);
  CHECK_EQ(c.wait(), 1);
}

void test_qa_version_with_unexecutable_clangd() {
  LspClient c({g_lsp, "--clangd=/nonexistent/clangd", "--version"});
  c.close_input();
  CHECK_EQ(c.read_all(), "");
  CHECK_EQ(c.wait(), 1);
}

// --- QA round 2 ----------------------------------------------------------------

void test_qa2_exit_code_is_clangds_after_exit() {
  // After `exit` was forwarded, the proxy's status is clangd's, not a fixed 0.
  LspClient c({g_lsp, "--clangd=/bin/sh", "--", "-c", "cat >/dev/null; exit 7"});
  CHECK(c.send(kExit));
  c.close_input();
  CHECK_EQ(c.read_all(), "");
  CHECK_EQ(c.wait(), 7);
}

void test_qa2_server_closes_stdout_but_lives() {
  // clangd's stdout EOF is the crash signal even when the process lingers:
  // the editor gets the report (after the SIGTERM grace) and the proxy exits 1.
  LspClient c({g_lsp, "--clangd=/bin/sh", "--", "-c", "exec >&-; exec sleep 30"});
  CHECK(c.send(kInit));
  std::string all = c.read_all();
  CHECK(contains(all, "clangd exited unexpectedly"));
  CHECK(contains(all, "\"id\":1")); // the pending initialize is failed, not left hanging
  CHECK_EQ(c.wait(), 1);
}

void test_qa2_pipelined_shutdown_exit_then_eof() {
  // Editors write shutdown+exit and close the pipe at once. The client EOF
  // races clangd's own exit; it must never be reported as a crash and every
  // reply must still be delivered.
  for (int i = 0; i < 25; ++i) {
    LspClient c({g_lsp, "--clangd=" + g_fake});
    CHECK(c.send_raw(asynctp::test::frame(kInit) + asynctp::test::frame(kShutdown) + asynctp::test::frame(kExit)));
    c.close_input();
    std::string all = c.read_all();
    CHECK_EQ(all, asynctp::test::frame(R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{}}})") +
                      asynctp::test::frame(R"({"jsonrpc":"2.0","id":"s-2","result":null})"));
    CHECK_EQ(c.wait(), 0);
  }
}

void test_qa2_exit_with_client_stdin_left_open() {
  // Some editors keep the pipe open after `exit`; the proxy must still end.
  LspClient c({g_lsp, "--clangd=" + g_fake});
  CHECK(c.send(kShutdown));
  CHECK(c.send(kExit));
  CHECK_EQ(c.read_all(), asynctp::test::frame(R"({"jsonrpc":"2.0","id":"s-2","result":null})"));
  CHECK_EQ(c.wait(std::chrono::seconds(5)), 0);
}

void test_qa2_odd_client_bodies_pass_through() {
  // Empty and non-JSON bodies are not the proxy's business: forward them
  // and keep going. `exit` nested in params is not `exit`.
  // `cat` as the server records exactly the bytes clangd would receive.
  TempDir t;
  fs::path received = t / "received.bin";
  LspClient c({g_lsp, "--clangd=/bin/sh", "--", "-c", "cat > \"$0\"", received.string()});
  std::string nested = R"({"jsonrpc":"2.0","method":"x/y","params":{"method":"exit"}})";
  CHECK(c.send(nested));
  CHECK(c.send(""));
  CHECK(c.send("not json"));
  CHECK(c.send(kExit));
  c.close_input();
  CHECK_EQ(c.read_all(), "");
  CHECK_EQ(c.wait(), 0);
  using asynctp::test::frame;
  CHECK_EQ(read_file(received), frame(nested) + frame("") + frame("not json") + frame(kExit));
}

void test_qa2_client_garbage_header_ends_session() {
  LspClient c({g_lsp, "--clangd=" + g_fake});
  CHECK(c.send_raw("GARBAGE\n\n"));
  CHECK_EQ(c.recv().value_or("<eof>"), "<eof>");
  CHECK_EQ(c.wait(), 1);
}

// --- run_proxy in-process: the hook seam ----------------------------------

void test_hooks_rewrite_and_swallow() {
  TempDir t;
  fs::path transcript = t / "transcript.txt";
  ::setenv("FAKE_LSP_TRANSCRIPT", transcript.c_str(), 1);

  std::string drop = R"({"jsonrpc":"2.0","method":"fake/drop"})";
  std::istringstream client_in(asynctp::test::frame(kInit) + asynctp::test::frame(drop) +
                               asynctp::test::frame(R"({"jsonrpc":"2.0","method":"fake/echo","params":"a"})") +
                               asynctp::test::frame(kShutdown) + asynctp::test::frame(kExit));
  std::ostringstream client_out, log;

  asynctp::lsp::Hooks hooks;
  hooks.client_to_server = [](std::string&& body) -> std::optional<std::string> {
    if (contains(body, "fake/drop")) return std::nullopt;
    if (contains(body, R"("params":"a")")) return R"({"jsonrpc":"2.0","method":"fake/echo","params":"b"})";
    return std::move(body);
  };
  hooks.server_to_client = [](std::string&& body) -> std::optional<std::string> {
    if (contains(body, "\"result\":null")) return std::nullopt; // swallow the shutdown reply
    return std::move(body);
  };

  int code = asynctp::lsp::run_proxy(g_fake, {}, client_in, client_out, hooks, &log);
  CHECK_EQ(code, 0);
  CHECK_EQ(read_file(transcript),
           kInit + "\n" + R"({"jsonrpc":"2.0","method":"fake/echo","params":"b"})" + "\n" + kShutdown + "\n" + kExit + "\n");
  CHECK_EQ(client_out.str(),
           asynctp::test::frame(R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{}}})") +
               asynctp::test::frame(R"({"jsonrpc":"2.0","method":"fake/echoed","params":"b"})"));

  // Log: one line per message with direction, method or id, byte count; no bodies.
  std::string l = log.str();
  CHECK(contains(l, "--> initialize " + std::to_string(kInit.size()) + "\n"));
  CHECK(contains(l, "--> exit "));
  CHECK(contains(l, "<-- id=1 "));
  CHECK(contains(l, "<-- fake/echoed "));
  CHECK(!contains(l, "capabilities"));
  ::unsetenv("FAKE_LSP_TRANSCRIPT");
}

void test_in_process_client_eof_returns_1() {
  std::istringstream client_in(asynctp::test::frame(kInit));
  std::ostringstream client_out;
  CHECK_EQ(asynctp::lsp::run_proxy(g_fake, {}, client_in, client_out, {}, nullptr), 1);
  CHECK_EQ(client_out.str(), asynctp::test::frame(R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{}}})"));
}

void test_run_without_clangd() {
  std::istringstream client_in(asynctp::test::frame(R"({"jsonrpc":"2.0","method":"initialized"})") +
                               asynctp::test::frame(R"({"jsonrpc":"2.0","id":7,"method":"initialize"})") +
                               asynctp::test::frame(kExit) +
                               asynctp::test::frame(R"({"jsonrpc":"2.0","id":8,"method":"never"})"));
  std::ostringstream client_out;
  CHECK_EQ(asynctp::lsp::run_without_clangd("boom", client_in, client_out, nullptr), 1);
  CHECK_EQ(client_out.str(),
           asynctp::test::frame(R"({"jsonrpc":"2.0","id":7,"error":{"code":-32603,"message":"boom"}})") +
               asynctp::test::frame(R"({"jsonrpc":"2.0","method":"window/showMessage","params":{"type":1,"message":"boom"}})"));
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: lsp_proxy_tests <path-to-asynctp-lsp> <path-to-fake_lsp_server>\n";
    return 2;
  }
  // A child that dies mid-test must show up as a failed write, not kill us.
  std::signal(SIGPIPE, SIG_IGN);
  g_lsp = fs::absolute(argv[1]).string();
  g_fake = fs::absolute(argv[2]).string();

  test_discover_precedence();
  test_discover_path_order_and_executable_bit();
  test_discover_skips_path_clangd_that_is_self();
  test_discover_flag_or_env_at_self_is_error();
  test_discover_bare_flag_resolves_like_execvp();
  test_discover_highest_numbered();
  test_discover_nothing_found();
  test_split_path_list();

  test_session_round_trip();
  test_message_split_across_writes();
  test_client_eof_without_exit_returns_1();
  test_clangd_crash_is_reported();
  test_crash_before_any_reply_fails_initialize();
  test_nonexistent_clangd_fails_loudly();
  test_clangd_not_found_fails_loudly();
  test_env_var_selects_clangd();
  test_flag_at_self_fails_loudly();
  test_version();
  test_help();
  test_unknown_options_are_forwarded();

  test_qa_discover_bare_name_that_execvp_resolves_to_self();
  test_qa_exit_inside_string_is_not_exit();
  test_qa_large_message_both_directions();
  test_qa_pipelined_messages_keep_order();
  test_qa_client_eof_mid_frame();
  test_qa_server_garbage_is_reported();
  test_qa_server_ignores_stdin_eof();
  test_qa_client_keeps_writing_after_crash();
  test_qa_version_with_unexecutable_clangd();

  test_qa2_exit_code_is_clangds_after_exit();
  test_qa2_server_closes_stdout_but_lives();
  test_qa2_pipelined_shutdown_exit_then_eof();
  test_qa2_exit_with_client_stdin_left_open();
  test_qa2_odd_client_bodies_pass_through();
  test_qa2_client_garbage_header_ends_session();

  test_hooks_rewrite_and_swallow();
  test_in_process_client_eof_returns_1();
  test_run_without_clangd();

  return asynctp::test::report("lsp_proxy_tests");
}
