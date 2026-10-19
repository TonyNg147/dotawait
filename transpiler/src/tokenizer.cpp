#include "asynctp/tokenizer.hpp"

#include <algorithm>
#include <string>

namespace asynctp {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool is_ident_start(char c) {
  unsigned char u = static_cast<unsigned char>(c);
  return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_' || u == '$' || u >= 0x80;
}

bool is_ident_char(char c) { return is_ident_start(c) || is_digit(c); }

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

bool is_encoding_prefix(std::string_view id) { return id == "u8" || id == "u" || id == "U" || id == "L"; }

bool is_raw_prefix(std::string_view id) {
  return id == "R" || id == "u8R" || id == "uR" || id == "UR" || id == "LR";
}

class Lexer {
public:
  explicit Lexer(std::string_view source) { out_.source = source; }

  std::variant<TokenizedFile, ParseError> run() {
    compute_line_starts();
    bool line_start = true; // only whitespace/comments since the last newline

    while (pos_ < src().size()) {
      char c = src()[pos_];

      if (is_space(c)) {
        if (c == '\n') line_start = true;
        pos_++;
        continue;
      }

      std::size_t begin = pos_;

      if (c == '/' && peek(1) == '/') {
        skip_line_comment();
        push(TokKind::Comment, begin);
        continue;
      }
      if (c == '/' && peek(1) == '*') {
        if (!skip_block_comment()) return error_at(begin, "unterminated block comment");
        // A comment is whitespace; one that spans lines leaves the next
        // token first on its line.
        if (src().substr(begin, pos_ - begin).find('\n') != std::string_view::npos) line_start = true;
        push(TokKind::Comment, begin);
        continue;
      }

      if (c == '#' && line_start) {
        if (!skip_directive()) return error_at(error_offset_, error_message_);
        push(TokKind::Directive, begin);
        continue;
      }
      line_start = false;

      if (is_digit(c) || (c == '.' && is_digit(peek(1)))) {
        skip_pp_number();
        push(TokKind::Number, begin);
        continue;
      }

      if (is_ident_start(c)) {
        while (pos_ < src().size() && is_ident_char(src()[pos_])) pos_++;
        std::string_view id = src().substr(begin, pos_ - begin);
        char next = peek(0);
        if (next == '"' && is_raw_prefix(id)) {
          if (!skip_raw_string()) return error_at(begin, "unterminated raw string literal");
          push(TokKind::String, begin);
          continue;
        }
        if ((next == '"' || next == '\'') && is_encoding_prefix(id)) {
          if (!lex_quoted(begin)) {
            // Unterminated: the prefix stays an identifier and the Invalid
            // token starts at the quote (see the policy in tokenizer.hpp).
            out_.toks.push_back(Tok{TokKind::Identifier, begin, quote_offset_});
            push(TokKind::Invalid, quote_offset_);
          }
          continue;
        }
        push(TokKind::Identifier, begin);
        continue;
      }

      if (c == '"' || c == '\'') {
        if (!lex_quoted(begin)) push(TokKind::Invalid, begin);
        continue;
      }

      pos_ += punct_length();
      push(TokKind::Punct, begin);
    }

    return std::move(out_);
  }

private:
  std::string_view src() const { return out_.source; }

  char peek(std::size_t ahead) const {
    std::size_t p = pos_ + ahead;
    return p < src().size() ? src()[p] : '\0';
  }

  void push(TokKind kind, std::size_t begin) { out_.toks.push_back(Tok{kind, begin, pos_}); }

  ParseError error_at(std::size_t offset, std::string message) const {
    auto loc = location_of(out_, offset);
    return ParseError{offset, loc.line, loc.col, std::move(message)};
  }

  void compute_line_starts() {
    out_.line_starts.push_back(0);
    for (std::size_t i = 0; i < src().size(); ++i) {
      if (src()[i] == '\n') out_.line_starts.push_back(i + 1);
    }
  }

  // Length of a backslash-newline (phase-2 splice) at pos_, or 0.
  std::size_t splice_length() const {
    if (peek(0) != '\\') return 0;
    if (peek(1) == '\n') return 2;
    if (peek(1) == '\r' && peek(2) == '\n') return 3;
    return 0;
  }

  // Stops at the terminating newline (not consumed); a backslash-newline
  // continues the comment, as in translation phase 2.
  void skip_line_comment() {
    while (pos_ < src().size()) {
      if (std::size_t n = splice_length()) {
        pos_ += n;
        continue;
      }
      if (src()[pos_] == '\n') return;
      pos_++;
    }
  }

  // Precondition: pos_ is at "/*". Returns false if it never closes.
  bool skip_block_comment() {
    std::size_t close = src().find("*/", pos_ + 2);
    if (close == std::string_view::npos) return false;
    pos_ = close + 2;
    return true;
  }

  // Precondition: pos_ is at '#'. Runs to the end of the logical line, which
  // a `/* */` or raw string starting in it may carry past physical newlines
  // (GCC keeps both in the directive). Only an unterminated block comment or
  // raw string fails; error_offset_/error_message_ describe it.
  bool skip_directive() {
    pos_++; // '#'
    while (pos_ < src().size()) {
      if (std::size_t n = splice_length()) {
        pos_ += n;
        continue;
      }
      char c = src()[pos_];
      if (c == '\n') return true;
      if (c == '/' && peek(1) == '*') {
        error_offset_ = pos_;
        error_message_ = "unterminated block comment";
        if (!skip_block_comment()) return false;
        continue;
      }
      if (c == '/' && peek(1) == '/') {
        skip_line_comment();
        return true;
      }
      if (c == '"' || c == '\'') {
        // Lenient: a lone quote just runs to the end of the line.
        quote_offset_ = pos_;
        skip_quoted_body(c);
        continue;
      }
      if (is_digit(c) || (c == '.' && is_digit(peek(1)))) {
        skip_pp_number(); // so `1'000` doesn't open a char literal
        continue;
      }
      if (is_ident_start(c)) {
        std::size_t id_begin = pos_;
        while (pos_ < src().size() && is_ident_char(src()[pos_])) pos_++;
        if (peek(0) == '"' && is_raw_prefix(src().substr(id_begin, pos_ - id_begin))) {
          // Strict, unlike ordinary quotes: a raw string's newlines are part
          // of the literal and must not end the directive.
          error_offset_ = id_begin;
          error_message_ = "unterminated raw string literal";
          if (!skip_raw_string()) return false;
        }
        continue;
      }
      pos_++;
    }
    return true;
  }

  // [lex.ppnumber]: digit or .digit, then digits, identifier characters,
  // `'` + digit/identifier character, e/E/p/P + sign, and `.`.
  void skip_pp_number() {
    pos_++;
    while (pos_ < src().size()) {
      char c = src()[pos_];
      if ((c == 'e' || c == 'E' || c == 'p' || c == 'P') && (peek(1) == '+' || peek(1) == '-')) {
        pos_ += 2;
      } else if (is_ident_char(c) || c == '.') {
        pos_++;
      } else if (c == '\'' && is_ident_char(peek(1))) {
        pos_ += 2;
      } else {
        return;
      }
    }
  }

  // Precondition: pos_ is at the opening quote. Consumes through the closing
  // quote (true) or stops at the newline/EOF that ends the line (false).
  // A backslash escapes the next character, newline included (a splice).
  bool skip_quoted_body(char quote) {
    pos_++;
    while (pos_ < src().size()) {
      char c = src()[pos_];
      if (c == '\\') {
        if (std::size_t n = splice_length()) {
          pos_ += n;
        } else {
          pos_ += pos_ + 1 < src().size() ? 2 : 1;
        }
        continue;
      }
      if (c == '\n') return false;
      pos_++;
      if (c == quote) return true;
    }
    return false;
  }

  // Lexes an ordinary string/char literal whose token starts at `begin`
  // (the prefix, if any) and whose quote is at pos_. On success pushes the
  // String/Char token, ud-suffix included. On failure records the quote
  // offset in quote_offset_ and leaves pos_ at the end of the line.
  bool lex_quoted(std::size_t begin) {
    char quote = src()[pos_];
    quote_offset_ = pos_;
    if (!skip_quoted_body(quote)) return false;
    if (pos_ < src().size() && is_ident_start(src()[pos_])) {
      while (pos_ < src().size() && is_ident_char(src()[pos_])) pos_++;
    }
    push(quote == '"' ? TokKind::String : TokKind::Char, begin);
    return true;
  }

  // Precondition: pos_ is at the '"' after an R-prefix. Consumes
  // `"delim( ... )delim"`; false if the delimiter or the body never closes.
  bool skip_raw_string() {
    std::size_t delim_start = pos_ + 1;
    std::size_t open = delim_start;
    while (open < src().size() && src()[open] != '(' && src()[open] != '\n') open++;
    if (open >= src().size() || src()[open] != '(') return false;
    std::string closing = ")" + std::string(src().substr(delim_start, open - delim_start)) + "\"";
    std::size_t close = src().find(closing, open + 1);
    if (close == std::string_view::npos) return false;
    pos_ = close + closing.size();
    return true;
  }

  std::size_t punct_length() const {
    char c = peek(0);
    char n = peek(1);
    if (c == ':' && n == ':') return 2;
    if (c == '-' && n == '>') return peek(2) == '*' ? 3 : 2;
    if (c == '.' && n == '.' && peek(2) == '.') return 3;
    if (c == '.' && n == '*') return 2;
    return 1;
  }

  TokenizedFile out_;
  std::size_t pos_ = 0;
  std::size_t quote_offset_ = 0; // opening quote of the last quoted scan
  std::size_t error_offset_ = 0; // unterminated "/*" or raw string in the current directive
  const char* error_message_ = "";
};

} // namespace

std::variant<TokenizedFile, ParseError> tokenize(std::string_view source) { return Lexer(source).run(); }

SourceLocation location_of(const TokenizedFile& file, std::size_t offset) {
  std::size_t clamped = std::min(offset, file.source.size());
  if (file.line_starts.empty()) return SourceLocation{1, clamped + 1}; // not produced by tokenize()
  auto it = std::upper_bound(file.line_starts.begin(), file.line_starts.end(), clamped);
  std::size_t line_index = static_cast<std::size_t>(it - file.line_starts.begin()) - 1;
  return SourceLocation{line_index + 1, clamped - file.line_starts[line_index] + 1};
}

} // namespace asynctp
