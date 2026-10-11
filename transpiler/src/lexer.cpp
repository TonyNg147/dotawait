#include "asynctp/lexer.hpp"

#include "asynctp/scanner.hpp"

#include <cctype>

namespace asynctp {

std::vector<std::size_t> find_async_markers(std::string_view source) {
  std::vector<std::size_t> markers;
  SourceCursor cur(source);

  while (!cur.at_end()) {
    cur.skip_whitespace_and_comments();
    if (cur.has_error() || cur.at_end()) break;

    char c = cur.peek();
    if (c == '"' || c == '\'') {
      cur.skip_string_or_char_literal();
      if (cur.has_error()) break;
      continue;
    }

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      std::size_t start = cur.offset();
      std::string_view id = cur.scan_identifier_if_present();
      if (id == "async") {
        markers.push_back(start);
      }
      continue;
    }

    cur.advance_one();
  }

  return markers;
}

} // namespace asynctp
