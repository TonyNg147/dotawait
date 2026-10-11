#pragma once

#include "ast.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

namespace asynctp {

struct ParseError {
  std::size_t offset;
  std::size_t line;
  std::size_t col;
  std::string message;
};

// Parses `source` into a ParsedFile using find_async_markers (lexer.hpp) to
// locate every `async` keyword, then, for each one, applying the decided
// function-name heuristic: scan forward for an identifier immediately
// followed by a balanced `(...)` immediately followed by `{` -- that
// identifier is the function name, everything between `async` and it is the
// return type, and the matching `{...}` is the body.
//
// Known MVP limitation: a return type containing its own unbalanced/top-level
// parens (e.g. a raw function-pointer return type) breaks this heuristic. In
// that case no identifier will satisfy the pattern before EOF, and parsing
// fails with a ParseError rather than silently emitting incorrect C++.
//
// Never returns a partially-valid ParsedFile on error: either every marked
// function in the file was extracted successfully, or the first failure is
// returned and nothing is emitted.
std::variant<ParsedFile, ParseError> parse_file(std::string_view source);

} // namespace asynctp
