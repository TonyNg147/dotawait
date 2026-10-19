// Optional end-to-end check of asynctp-lsp against the real clangd: finds
// clangd exactly the way asynctp-lsp does (ASYNCTP_CLANGD, then PATH, then
// clangd-NN), checks `--version`, and runs initialize/shutdown/exit through
// the proxy. Exits 77 (ctest SKIP_RETURN_CODE, the Makefile prints SKIPPED)
// when there is no clangd, per decision D4.
//
// Usage: lsp_clangd_smoke <path-to-asynctp-lsp>

#include "asynctp/lsp/json.hpp"
#include "asynctp/lsp/proxy.hpp"
#include "lsp_test_client.hpp"
#include "test_support.hpp"

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using asynctp::lsp::Json;
using asynctp::test::LspClient;

namespace {

// Reads until the response with this id, skipping server notifications and
// requests (clangd may send e.g. window/workDoneProgress/create).
Json await_response(LspClient& c, std::int64_t id) {
  while (auto body = c.recv()) {
    auto r = Json::parse(*body);
    if (auto* j = std::get_if<Json>(&r)) {
      const Json* got = j->find("id");
      if (got && got->as_int(-1) == id && !j->find("method")) return *j;
    }
  }
  return Json();
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: lsp_clangd_smoke <path-to-asynctp-lsp>\n";
    return 2;
  }
  std::signal(SIGPIPE, SIG_IGN);
  std::string lsp = fs::absolute(argv[1]).string();

  std::error_code ec;
  std::optional<fs::path> self = fs::weakly_canonical(lsp, ec);
  const char* env = std::getenv("ASYNCTP_CLANGD");
  const char* path = std::getenv("PATH");
  auto clangd = asynctp::lsp::discover_clangd(std::nullopt, env ? std::optional<std::string>(env) : std::nullopt,
                                              asynctp::lsp::split_path_list(path ? path : ""), self);
  if (!clangd) {
    std::cout << "clangd not found; skipping\n";
    return 77;
  }
  std::cout << "lsp_clangd_smoke: using " << clangd->string() << "\n";

  {
    LspClient c({lsp, "--clangd=" + clangd->string(), "--version"});
    CHECK(c.started());
    std::string out = c.read_all();
    CHECK_EQ(c.wait(), 0);
    CHECK(out.find("clangd version") != std::string::npos);
    CHECK(out.find("asynctp-lsp ") != std::string::npos);
  }

  {
    // `--log=error` after `--` goes to clangd (keeps ctest output quiet);
    // before `--` it would be asynctp-lsp's own --log=<file>.
    LspClient c({lsp, "--clangd=" + clangd->string(), "--", "--log=error"});
    CHECK(c.started());
    CHECK(c.send(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,"capabilities":{}}})"));
    Json init = await_response(c, 1);
    const Json* result = init.find("result");
    CHECK(result && result->find("capabilities") && result->find("capabilities")->is_object());
    CHECK(c.send(R"({"jsonrpc":"2.0","method":"initialized","params":{}})"));
    CHECK(c.send(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})"));
    Json shut = await_response(c, 2);
    CHECK(shut.find("result") && shut.find("result")->is_null());
    CHECK(c.send(R"({"jsonrpc":"2.0","method":"exit"})"));
    CHECK_EQ(c.wait(), 0);
  }

  {
    // QA: a real editing session, proxied vs direct. With identity hooks
    // every server body must come through byte-identical, including the
    // asynchronous publishDiagnostics that races the client's requests.
    fs::path ws = fs::temp_directory_path() / "asynctp_lsp_smoke_ws";
    fs::create_directories(ws);
    {
      std::ofstream(ws / "a.cpp") << "int add(int a, int b) { return a + b; }\n"
                                     "int main() { return add(1, undefined_thing); }\n";
    }
    std::string root = "file://" + ws.string();
    std::string uri = root + "/a.cpp";
    struct Seen {
      std::string symbols, hover, diagnostics;
      int rc = -99;
    };
    auto session = [&](const std::vector<std::string>& argv) {
      Seen seen;
      LspClient c(argv);
      CHECK(c.send(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":")" +
                   root + R"(","capabilities":{}}})"));
      await_response(c, 1);
      CHECK(c.send(R"({"jsonrpc":"2.0","method":"initialized","params":{}})"));
      CHECK(c.send(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" + uri +
                   R"(","languageId":"cpp","version":1,"text":"int add(int a, int b) { return a + b; }\nint main() { return add(1, undefined_thing); }\n"}}})"));
      CHECK(c.send(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":")" +
                   uri + R"("}}})"));
      CHECK(c.send(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{"textDocument":{"uri":")" + uri +
                   R"("},"position":{"line":1,"character":21}}})"));
      while (seen.symbols.empty() || seen.hover.empty() || seen.diagnostics.empty()) {
        auto body = c.recv();
        if (!body) break;
        auto r = Json::parse(*body);
        auto* j = std::get_if<Json>(&r);
        if (!j) continue;
        const Json* id = j->find("id");
        if (id && id->as_int(-1) == 2) seen.symbols = *body;
        if (id && id->as_int(-1) == 3) seen.hover = *body;
        if (body->find("publishDiagnostics") != std::string::npos &&
            body->find("undefined_thing") != std::string::npos) {
          seen.diagnostics = *body;
        }
      }
      CHECK(c.send(R"({"jsonrpc":"2.0","id":4,"method":"shutdown"})"));
      await_response(c, 4);
      CHECK(c.send(R"({"jsonrpc":"2.0","method":"exit"})"));
      seen.rc = c.wait();
      return seen;
    };
    Seen direct = session({clangd->string(), "--log=error"});
    Seen proxied = session({lsp, "--clangd=" + clangd->string(), "--", "--log=error"});
    CHECK(!direct.symbols.empty() && !direct.hover.empty() && !direct.diagnostics.empty());
    CHECK_EQ(proxied.symbols, direct.symbols);
    CHECK_EQ(proxied.hover, direct.hover);
    CHECK_EQ(proxied.diagnostics, direct.diagnostics);
    CHECK_EQ(direct.rc, 0);
    CHECK_EQ(proxied.rc, 0);
    fs::remove_all(ws, ec);
  }

  return asynctp::test::report("lsp_clangd_smoke");
}
