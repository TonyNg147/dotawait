// A stand-in for clangd in lsp_proxy_tests: speaks just enough LSP to drive
// the proxy through a session, and records exactly what it received.
//
//   - every incoming body is appended, plus "\n", to $FAKE_LSP_TRANSCRIPT
//     (if set), opened per message so it is complete even after a crash;
//   - initialize -> {"jsonrpc":"2.0","id":<id>,"result":{"capabilities":{}}}
//   - shutdown   -> result null
//   - exit       -> exit 0
//   - fake/echo  -> notification fake/echoed with the same params
//   - fake/crash -> exit 3 immediately
//   - --version  -> prints "clangd version 99.0.0 (fake)"
//   - $FAKE_LSP_ARGV, if set, gets the command-line arguments, one per line

#include "asynctp/lsp/framing.hpp"
#include "asynctp/lsp/json.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

using asynctp::lsp::Json;

namespace {

void reply(const Json& id, Json result) {
  Json r;
  r["jsonrpc"] = "2.0";
  r["id"] = id;
  r["result"] = std::move(result);
  asynctp::lsp::write_message(std::cout, r.dump());
}

} // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--version") {
    std::cout << "clangd version 99.0.0 (fake)\n";
    return 0;
  }
  // Lets tests see which arguments the proxy forwarded, one per line.
  if (const char* argv_file = std::getenv("FAKE_LSP_ARGV")) {
    std::ofstream out(argv_file, std::ios::binary);
    for (int i = 1; i < argc; ++i) out << argv[i] << "\n";
  }
  std::ios::sync_with_stdio(false);
  const char* transcript = std::getenv("FAKE_LSP_TRANSCRIPT");

  for (;;) {
    std::string error;
    auto body = asynctp::lsp::read_message(std::cin, &error);
    if (!body) {
      if (!error.empty()) std::cerr << "fake_lsp_server: " << error << "\n";
      return error.empty() ? 0 : 2;
    }
    if (transcript) {
      std::ofstream out(transcript, std::ios::binary | std::ios::app);
      out << *body << "\n";
    }

    auto parsed = Json::parse(*body);
    if (!std::holds_alternative<Json>(parsed)) {
      std::cerr << "fake_lsp_server: unparsable body\n";
      return 2;
    }
    const Json& msg = std::get<Json>(parsed);
    const Json* method = msg.find("method");
    std::string_view m = method ? method->as_string() : std::string_view{};
    const Json* id = msg.find("id");

    if (m == "initialize" && id) {
      Json caps;
      caps["capabilities"] = Json(Json::Object{});
      reply(*id, std::move(caps));
    } else if (m == "shutdown" && id) {
      reply(*id, nullptr);
    } else if (m == "exit") {
      return 0;
    } else if (m == "fake/echo") {
      Json n;
      n["jsonrpc"] = "2.0";
      n["method"] = "fake/echoed";
      const Json* params = msg.find("params");
      n["params"] = params ? *params : Json();
      asynctp::lsp::write_message(std::cout, n.dump());
    } else if (m == "fake/crash") {
      std::cout.flush();
      std::_Exit(3);
    }
  }
}
