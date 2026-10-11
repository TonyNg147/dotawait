#pragma once

#include <cstddef>
#include <string_view>

namespace asynctp {

enum class TokenKind {
  Identifier,
  Punct,
  StringLiteral,
  CharLiteral,
  Comment,
  Whitespace,
  Other,
  EndOfFile
};

struct Token {
  TokenKind kind;
  std::string_view text;
  std::size_t offset;
  std::size_t line;
  std::size_t col;
};

} // namespace asynctp
