#include "asynctp/lsp/framing.hpp"

#include <algorithm>
#include <charconv>
#include <istream>
#include <ostream>
#include <system_error>

namespace asynctp::lsp {

namespace {

std::optional<std::string> fail(std::string* error, std::string message) {
  if (error) *error = std::move(message);
  return std::nullopt;
}

bool iequals_ascii(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
}

std::string_view trim_spaces(std::string_view s) noexcept {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

} // namespace

std::optional<std::string> read_message(std::istream& in, std::string* error) {
  if (error) error->clear();

  std::optional<std::size_t> length;
  bool first_line = true;
  std::string line;
  for (;;) {
    if (!std::getline(in, line)) {
      // Nothing extracted at all. Before the first header byte that is the
      // peer closing the stream between messages, which is not an error.
      if (first_line) return std::nullopt;
      return fail(error, "unexpected end of stream in message header");
    }
    if (in.eof()) return fail(error, "unexpected end of stream in message header");
    first_line = false;
    if (line.empty() || line.back() != '\r') {
      return fail(error, "message header line does not end with \\r\\n");
    }
    line.pop_back();
    if (line.empty()) break; // end of the header block

    std::string_view header(line);
    std::size_t colon = header.find(':');
    if (colon == std::string_view::npos) {
      return fail(error, "malformed message header (no ':'): '" + line + "'");
    }
    // Only Content-Length matters; Content-Type and anything else is ignored.
    if (!iequals_ascii(header.substr(0, colon), "Content-Length")) continue;
    if (length) return fail(error, "duplicate Content-Length header");
    std::string_view value = trim_spaces(header.substr(colon + 1));
    std::size_t n = 0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
    if (value.empty() || ec != std::errc{} || ptr != value.data() + value.size()) {
      return fail(error, "invalid Content-Length value '" + std::string(value) + "'");
    }
    length = n;
  }
  if (!length) return fail(error, "missing Content-Length header");

  // Grow the buffer with the bytes that actually arrive rather than trusting
  // the header up front, so a bogus huge length on a short stream is a
  // "truncated" error instead of a failed giant allocation.
  constexpr std::size_t kChunk = 64 * 1024;
  std::string body;
  while (body.size() < *length) {
    std::size_t old = body.size();
    std::size_t want = std::min(kChunk, *length - old);
    body.resize(old + want);
    in.read(body.data() + old, static_cast<std::streamsize>(want));
    auto got = static_cast<std::size_t>(in.gcount());
    if (got < want) {
      return fail(error, "truncated message body: expected " + std::to_string(*length) +
                             " bytes, got " + std::to_string(old + got));
    }
  }
  return body;
}

void write_message(std::ostream& out, std::string_view json) {
  // to_chars rather than `<<` so an imbued locale can't add digit grouping.
  char digits[24];
  auto r = std::to_chars(digits, digits + sizeof digits, json.size());
  out << "Content-Length: ";
  out.write(digits, r.ptr - digits);
  out << "\r\n\r\n";
  out.write(json.data(), static_cast<std::streamsize>(json.size()));
  out.flush();
}

} // namespace asynctp::lsp
