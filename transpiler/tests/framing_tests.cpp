#include "asynctp/lsp/framing.hpp"
#include "test_support.hpp"

#include <sstream>
#include <string>

using namespace asynctp::lsp;

namespace {

void test_two_back_to_back_messages() {
  std::stringstream ss("Content-Length: 7\r\n\r\n{\"a\":1}"
                       "Content-Length: 2\r\n\r\n[]");
  std::string error = "stale";
  auto first = read_message(ss, &error);
  CHECK(first.has_value());
  CHECK_EQ(first.value_or(""), "{\"a\":1}");
  CHECK(error.empty());
  auto second = read_message(ss, &error);
  CHECK_EQ(second.value_or(""), "[]");
  CHECK(error.empty());

  // Clean EOF between messages: nullopt and no error.
  error = "stale";
  CHECK(!read_message(ss, &error).has_value());
  CHECK(error.empty());
}

void test_empty_stream_is_clean_eof() {
  std::stringstream ss("");
  std::string error = "stale";
  CHECK(!read_message(ss, &error).has_value());
  CHECK(error.empty());
}

void test_header_name_is_case_insensitive() {
  std::stringstream ss("content-length: 2\r\n\r\n{}");
  std::string error;
  CHECK_EQ(read_message(ss, &error).value_or("<none>"), "{}");
  CHECK(error.empty());

  std::stringstream upper("CONTENT-LENGTH:2\r\n\r\n{}"); // no space after ':' is fine
  CHECK_EQ(read_message(upper, &error).value_or("<none>"), "{}");
}

void test_content_type_is_ignored() {
  std::stringstream ss("Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n"
                       "Content-Length: 4\r\n\r\nnull");
  std::string error;
  CHECK_EQ(read_message(ss, &error).value_or("<none>"), "null");
  CHECK(error.empty());

  std::stringstream after("Content-Length: 4\r\n"
                          "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n\r\nnull");
  CHECK_EQ(read_message(after, &error).value_or("<none>"), "null");
}

void test_missing_length_is_an_error() {
  std::stringstream ss("Content-Type: application/vscode-jsonrpc\r\n\r\n{}");
  std::string error;
  CHECK(!read_message(ss, &error).has_value());
  CHECK(error.find("Content-Length") != std::string::npos);

  std::stringstream bare("\r\n{}");
  error.clear();
  CHECK(!read_message(bare, &error).has_value());
  CHECK(!error.empty());
}

void test_truncated_body_is_an_error() {
  std::stringstream ss("Content-Length: 10\r\n\r\n{\"a\":");
  std::string error;
  CHECK(!read_message(ss, &error).has_value());
  CHECK(error.find("truncated") != std::string::npos);
}

void test_malformed_headers_are_errors() {
  const char* bad[] = {
      "Content-Length: abc\r\n\r\n{}",   // not a number
      "Content-Length: -2\r\n\r\n{}",    // negative
      "Content-Length: 2x\r\n\r\n{}",    // trailing junk
      "Content-Length: \r\n\r\n{}",      // empty value
      "Content-Length 2\r\n\r\n{}",      // no colon
      "Content-Length: 2\n\n{}",         // LF without CR
      "Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}", // duplicate
      "Content-Length: 99999999999999999999999\r\n\r\n{}", // overflows size_t
      "Content-Length: 2\r\n",           // EOF inside the header block
      "Content-Len",                     // EOF inside a header line
  };
  for (const char* text : bad) {
    std::stringstream ss(text);
    std::string error;
    bool got = read_message(ss, &error).has_value();
    CHECK(!got);
    CHECK(!error.empty());
    if (got || error.empty()) std::cerr << "  input: " << text << "\n";
  }
}

void test_null_error_pointer_is_allowed() {
  std::stringstream ok("Content-Length: 2\r\n\r\n{}");
  CHECK_EQ(read_message(ok, nullptr).value_or("<none>"), "{}");
  std::stringstream bad("Content-Length: 9\r\n\r\n{}");
  CHECK(!read_message(bad, nullptr).has_value());
}

void test_zero_length_body() {
  std::stringstream ss("Content-Length: 0\r\n\r\n");
  std::string error;
  auto msg = read_message(ss, &error);
  CHECK(msg.has_value());
  CHECK_EQ(msg.value_or("<none>"), "");
  CHECK(error.empty());
}

void test_write_message_counts_bytes() {
  std::ostringstream out;
  // 9 characters, 10 bytes: the e-acute is 2 bytes of UTF-8.
  std::string body = "{\"s\":\"\xC3\xA9\"}";
  write_message(out, body);
  CHECK_EQ(out.str(), "Content-Length: 10\r\n\r\n{\"s\":\"\xC3\xA9\"}");
}

void test_write_then_read_round_trip() {
  std::stringstream ss;
  write_message(ss, "{\"id\":1}");
  write_message(ss, "{\"id\":2,\"text\":\"\xF0\x9F\x98\x80\\r\\n\"}");
  std::string error;
  CHECK_EQ(read_message(ss, &error).value_or("<none>"), "{\"id\":1}");
  CHECK_EQ(read_message(ss, &error).value_or("<none>"),
           "{\"id\":2,\"text\":\"\xF0\x9F\x98\x80\\r\\n\"}");
  CHECK(!read_message(ss, &error).has_value());
  CHECK(error.empty());
}

void test_body_may_contain_crlf_bytes() {
  // The body is read by length, never by lines.
  std::stringstream ss("Content-Length: 6\r\n\r\n[\r\n\r\n]Content-Length: 2\r\n\r\n{}");
  std::string error;
  CHECK_EQ(read_message(ss, &error).value_or("<none>"), "[\r\n\r\n]");
  CHECK_EQ(read_message(ss, &error).value_or("<none>"), "{}");
}

// QA (T-0019): the body is read in 64 KiB chunks; every other test fits in
// one, so the multi-chunk path (and an exact chunk boundary) is covered here.
void test_body_larger_than_one_read_chunk() {
  for (std::size_t n : {std::size_t{65536}, std::size_t{65537}, std::size_t{200000}}) {
    std::string body(n, 'x');
    body.front() = '[';
    body.back() = ']';
    std::stringstream ss;
    write_message(ss, body);
    write_message(ss, "{}");
    std::string error;
    auto msg = read_message(ss, &error);
    CHECK(msg.has_value() && *msg == body);
    CHECK(error.empty());
    CHECK_EQ(read_message(ss, &error).value_or("<none>"), "{}");

    std::stringstream short_ss("Content-Length: " + std::to_string(n) + "\r\n\r\n" +
                               body.substr(0, n - 1));
    CHECK(!read_message(short_ss, &error).has_value());
    CHECK(error.find("truncated") != std::string::npos);
  }
}

void test_huge_declared_length_on_short_stream() {
  // Must report truncation, not attempt a SIZE_MAX allocation.
  std::stringstream ss("Content-Length: 18446744073709551615\r\n\r\n{}");
  std::string error;
  CHECK(!read_message(ss, &error).has_value());
  CHECK(error.find("truncated") != std::string::npos);
}

void test_more_header_edge_cases() {
  std::string error;
  std::stringstream padded("Content-Length: \t 2 \t\r\n\r\n{}");
  CHECK_EQ(read_message(padded, &error).value_or("<none>"), "{}");
  std::stringstream other_colons("X-Thing: a:b\r\n:\r\nContent-Length: 2\r\n\r\n{}");
  CHECK_EQ(read_message(other_colons, &error).value_or("<none>"), "{}");

  const char* bad[] = {
      "Content-Length: +2\r\n\r\n{}",
      "Content-Length: 0x2\r\n\r\n{}",
      "Content-Length: 2\r\r\n\r\n{}",   // stray CR is not trimmed into a valid length
      "Content-Length: 2\r\n\n{}",       // bare LF terminator line
      "\r",                              // partial line then EOF is not a clean EOF
      // Extra CRLF between messages is not part of the base protocol.
      "Content-Length: 2\r\n\r\n{}\r\nContent-Length: 2\r\n\r\n[]",
  };
  for (const char* text : bad) {
    std::stringstream ss(text);
    std::string last_error;
    std::optional<std::string> msg;
    int n = 0;
    while ((msg = read_message(ss, &last_error)) && n < 4) ++n;
    CHECK(!last_error.empty());
    if (last_error.empty()) std::cerr << "  input: " << text << "\n";
  }

  std::stringstream nul(std::string("Content-Length: 3\r\n\r\n{\0}", 25));
  auto msg = read_message(nul, &error);
  CHECK(msg.has_value() && *msg == std::string("{\0}", 3));
}

} // namespace

int main() {
  test_two_back_to_back_messages();
  test_empty_stream_is_clean_eof();
  test_header_name_is_case_insensitive();
  test_content_type_is_ignored();
  test_missing_length_is_an_error();
  test_truncated_body_is_an_error();
  test_malformed_headers_are_errors();
  test_null_error_pointer_is_allowed();
  test_zero_length_body();
  test_write_message_counts_bytes();
  test_write_then_read_round_trip();
  test_body_may_contain_crlf_bytes();
  test_body_larger_than_one_read_chunk();
  test_huge_declared_length_on_short_stream();
  test_more_header_edge_cases();
  return asynctp::test::report("framing_tests");
}
