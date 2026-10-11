#include "asynctp/async_parser.hpp"

#include "asynctp/lexer.hpp"
#include "asynctp/scanner.hpp"

#include <cctype>

namespace asynctp {

namespace {

constexpr std::size_t kAsyncKeywordLength = 5; // strlen("async")

std::string_view trim_view(std::string_view s) {
  std::size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
  std::size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
  return s.substr(start, end - start);
}

ParseError error_from(const ScanError& e) { return ParseError{e.offset, e.line, e.col, e.message}; }

// Applies the function-name heuristic starting just after one `async`
// keyword at `async_offset`. See async_parser.hpp for the algorithm summary.
std::variant<AsyncFunction, ParseError> parse_one(std::string_view source, std::size_t async_offset) {
  SourceCursor cur(source);
  cur.seek(async_offset + kAsyncKeywordLength);
  std::size_t return_type_start = cur.offset();

  while (true) {
    cur.skip_whitespace_and_comments();
    if (cur.has_error()) return error_from(cur.error());
    if (cur.at_end()) {
      auto loc = cur.location_at(async_offset);
      return ParseError{async_offset, loc.line, loc.col,
                         "could not find a function name, parameter list, and body after 'async'"};
    }

    char c = cur.peek();

    if (c == '"' || c == '\'') {
      cur.skip_string_or_char_literal();
      if (cur.has_error()) return error_from(cur.error());
      continue;
    }

    if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
      cur.advance_one();
      continue;
    }

    std::size_t id_start = cur.offset();
    std::string_view id = cur.scan_identifier_if_present();

    // Probe (on copies of the cursor, so a failed guess doesn't disturb the
    // main scan) whether `id` is immediately followed by a balanced `(...)`
    // immediately followed by `{`.
    SourceCursor probe = cur;
    probe.skip_whitespace_and_comments();
    if (probe.has_error() || probe.at_end() || probe.peek() != '(') continue;

    std::size_t open_paren = probe.offset();
    probe.advance_one(); // consume '('
    auto close = probe.find_matching('(', ')');
    if (!close.has_value()) continue; // this identifier wasn't it; keep scanning

    SourceCursor probe2 = probe;
    probe2.skip_whitespace_and_comments();
    if (probe2.has_error() || probe2.at_end() || probe2.peek() != '{') continue;

    probe2.advance_one(); // consume '{'
    std::size_t body_open_offset = probe2.offset();
    auto body_close = probe2.find_matching('{', '}');
    if (!body_close.has_value()) return error_from(probe2.error());

    AsyncFunction fn;
    fn.async_kw_offset = async_offset;
    fn.return_type = trim_view(source.substr(return_type_start, id_start - return_type_start));
    fn.name = id;
    fn.params = source.substr(open_paren, (*close + 1) - open_paren);
    fn.body_open_offset = body_open_offset;
    fn.body_close_offset = *body_close;
    fn.body = source.substr(body_open_offset, *body_close - body_open_offset);
    return fn;
  }
}

} // namespace

std::variant<ParsedFile, ParseError> parse_file(std::string_view source) {
  ParsedFile result;
  result.source = source;

  std::size_t consumed_until = 0;
  for (std::size_t async_offset : find_async_markers(source)) {
    if (async_offset < consumed_until) continue; // inside a function already extracted

    auto parsed = parse_one(source, async_offset);
    if (std::holds_alternative<ParseError>(parsed)) {
      return std::get<ParseError>(std::move(parsed));
    }

    AsyncFunction fn = std::get<AsyncFunction>(std::move(parsed));
    consumed_until = fn.body_close_offset + 1;
    result.functions.push_back(std::move(fn));
  }

  return result;
}

} // namespace asynctp
