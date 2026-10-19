#pragma once

#include "ast.hpp"
#include "diagnostics.hpp" // ParseError

#include <string_view>
#include <variant>

namespace asynctp {

// Tokenizes `source` once (tokenizer.hpp; its errors are returned as-is),
// uses find_async_markers (lexer.hpp) to locate every `async` keyword, then,
// for each one, applies the decided function-name heuristic over tokens:
// scan forward for an Identifier immediately followed (comments aside) by a
// balanced `(...)` immediately followed by `{` -- that identifier is the
// function name, everything between `async` and it is the return type, and
// the matching `{...}` is the body. Balancing counts only Punct tokens;
// literals, comments and directives are opaque.
//
// An Invalid token (a lone quote; see tokenizer.hpp) met anywhere in that
// forward scan -- return type, parameters, or body -- is reported as
// "unterminated string or character literal" at the quote. It must win over
// the generic error: the Invalid token swallows the rest of its line, `}`
// included, so balancing would otherwise run on and blame the wrong place.
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
