#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace asynctp {

struct SourceLocation {
  std::size_t line;
  std::size_t col;
};

struct ScanError {
  std::size_t offset;
  std::size_t line;
  std::size_t col;
  std::string message;
};

// Low-level cursor shared by the lexer and parser. It is NOT a C++ grammar
// engine: it only knows how to skip whitespace/comments, skip string and char
// literals (including raw strings), and balance-match a pair of delimiter
// characters while transparently skipping over any literal/comment it meets
// along the way. Every higher-level piece of the transpiler builds on these
// few primitives instead of re-implementing "don't get confused by a quote or
// a comment" in multiple places.
class SourceCursor {
public:
  explicit SourceCursor(std::string_view source) : source_(source) {}

  bool at_end() const noexcept { return pos_ >= source_.size(); }
  std::size_t offset() const noexcept { return pos_; }

  char peek(std::size_t ahead = 0) const noexcept {
    std::size_t p = pos_ + ahead;
    return p < source_.size() ? source_[p] : '\0';
  }

  // Consumes exactly one character, for callers walking content this cursor
  // has no special knowledge of (operators, numbers, punctuation, ...).
  void advance_one() noexcept { advance(); }

  // Advances past whitespace and `//`/`/* */` comments (non-nesting, as in
  // real C++), stopping at the first character that is neither.
  void skip_whitespace_and_comments();

  // Precondition: peek() is '"' or '\''. Advances past the full string or
  // char literal, including raw string forms `R"delim(...)delim"` (and the
  // u8R/uR/UR/LR prefix variants) when such a prefix immediately precedes the
  // opening quote. Sets an error on an unterminated literal.
  void skip_string_or_char_literal();

  // Precondition: `open_ch` has just been consumed by the caller (so this
  // cursor sits just after it). Scans forward balancing `open_ch`/`close_ch`,
  // skipping any string/char literal or comment encountered, and stops just
  // after the matching `close_ch`. Returns the offset of that matching
  // `close_ch`, or nullopt if the file ends first (an error is also set).
  std::optional<std::size_t> find_matching(char open_ch, char close_ch);

  // If the current position starts a C-style identifier
  // ([A-Za-z_][A-Za-z0-9_]*), consumes it and returns its text; otherwise
  // consumes nothing and returns an empty view.
  std::string_view scan_identifier_if_present();

  bool has_error() const noexcept { return error_.has_value(); }
  const ScanError& error() const noexcept { return *error_; }

  // Recomputes a 1-based line/col for a given byte offset, for diagnostics.
  SourceLocation location_at(std::size_t offset) const noexcept;

  // Repositions the cursor to an arbitrary byte offset (clamped to the end of
  // the source), recomputing line/col. Used by the parser to jump to each
  // `async` marker offset found by the lexer; does not clear any prior error.
  void seek(std::size_t offset) noexcept {
    pos_ = offset <= source_.size() ? offset : source_.size();
    auto loc = location_at(pos_);
    line_ = loc.line;
    col_ = loc.col;
  }

private:
  void advance() noexcept {
    if (pos_ >= source_.size()) return;
    if (source_[pos_] == '\n') {
      line_++;
      col_ = 1;
    } else {
      col_++;
    }
    pos_++;
  }

  void set_error(std::string message) {
    if (error_) return; // keep the first error
    error_ = ScanError{pos_, line_, col_, std::move(message)};
  }

  std::string_view source_;
  std::size_t pos_ = 0;
  std::size_t line_ = 1;
  std::size_t col_ = 1;
  std::optional<ScanError> error_;
};

} // namespace asynctp
