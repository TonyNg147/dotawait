#pragma once

#include <cstddef>
#include <string>

namespace asynctp {

// Kept in their own header so the tokenizer (which reports errors) and the
// AST (which stores the token stream) can both use them without
// tokenizer.hpp -> async_parser.hpp -> ast.hpp -> tokenizer.hpp cycling.

struct SourceLocation {
  std::size_t line; // 1-based
  std::size_t col;  // 1-based, in bytes
};

struct ParseError {
  std::size_t offset;
  std::size_t line;
  std::size_t col;
  std::string message;
};

} // namespace asynctp
