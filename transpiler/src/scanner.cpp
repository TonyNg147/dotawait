#include "asynctp/scanner.hpp"

#include <array>
#include <cctype>

namespace asynctp {

void SourceCursor::skip_whitespace_and_comments() {
  while (!at_end()) {
    char c = peek();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      advance();
      continue;
    }
    if (c == '/' && peek(1) == '/') {
      while (!at_end() && peek() != '\n') advance();
      continue;
    }
    if (c == '/' && peek(1) == '*') {
      advance();
      advance();
      bool closed = false;
      while (!at_end()) {
        if (peek() == '*' && peek(1) == '/') {
          advance();
          advance();
          closed = true;
          break;
        }
        advance();
      }
      if (!closed) {
        set_error("unterminated block comment");
        return;
      }
      continue;
    }
    break;
  }
}

void SourceCursor::skip_string_or_char_literal() {
  char quote = peek();
  bool is_double = quote == '"';

  if (is_double) {
    static constexpr std::array<std::string_view, 5> prefixes = {"u8R", "uR", "UR", "LR", "R"};
    for (auto pre : prefixes) {
      if (pos_ >= pre.size() && source_.substr(pos_ - pre.size(), pre.size()) == pre) {
        // Raw string literal: R"delim(...)delim"
        advance(); // consume opening '"'
        std::size_t delim_start = pos_;
        while (!at_end() && peek() != '(') advance();
        if (at_end()) {
          set_error("unterminated raw string literal delimiter");
          return;
        }
        std::string_view delim = source_.substr(delim_start, pos_ - delim_start);
        advance(); // consume '('
        std::string closing = ")" + std::string(delim) + "\"";
        while (true) {
          if (at_end()) {
            set_error("unterminated raw string literal");
            return;
          }
          if (peek() == ')' && source_.substr(pos_, closing.size()) == closing) {
            for (std::size_t i = 0; i < closing.size(); ++i) advance();
            return;
          }
          advance();
        }
      }
    }
  }

  // Ordinary string or char literal with escape handling.
  char closing_ch = quote;
  advance(); // consume opening quote
  while (true) {
    if (at_end()) {
      set_error("unterminated string or char literal");
      return;
    }
    char c = peek();
    if (c == '\\') {
      advance();
      if (at_end()) {
        set_error("unterminated string or char literal");
        return;
      }
      advance(); // consume the escaped character
      continue;
    }
    if (c == '\n') {
      set_error("unterminated string or char literal (unescaped newline)");
      return;
    }
    advance();
    if (c == closing_ch) return;
  }
}

std::optional<std::size_t> SourceCursor::find_matching(char open_ch, char close_ch) {
  int depth = 1;
  while (true) {
    skip_whitespace_and_comments();
    if (has_error()) return std::nullopt;
    if (at_end()) {
      set_error("unexpected end of file while matching delimiters");
      return std::nullopt;
    }
    char c = peek();
    if (c == '"' || c == '\'') {
      skip_string_or_char_literal();
      if (has_error()) return std::nullopt;
      continue;
    }
    if (c == open_ch) {
      depth++;
      advance();
      continue;
    }
    if (c == close_ch) {
      depth--;
      advance();
      if (depth == 0) return pos_ - 1;
      continue;
    }
    advance();
  }
}

std::string_view SourceCursor::scan_identifier_if_present() {
  if (at_end()) return {};
  char c = peek();
  if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_')) return {};
  std::size_t start = pos_;
  while (!at_end()) {
    char ch = peek();
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
      advance();
    } else {
      break;
    }
  }
  return source_.substr(start, pos_ - start);
}

SourceLocation SourceCursor::location_at(std::size_t offset) const noexcept {
  std::size_t line = 1, col = 1;
  std::size_t limit = offset < source_.size() ? offset : source_.size();
  for (std::size_t i = 0; i < limit; ++i) {
    if (source_[i] == '\n') {
      line++;
      col = 1;
    } else {
      col++;
    }
  }
  return {line, col};
}

} // namespace asynctp
