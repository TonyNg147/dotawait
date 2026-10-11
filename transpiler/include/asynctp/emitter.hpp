#pragma once

#include "ast.hpp"
#include "async_parser.hpp"

#include <string>
#include <variant>

namespace asynctp {

// Stitches the full output C++ source for a ParsedFile: passthrough text
// copied verbatim from the gaps between (and around) its AsyncFunction
// entries, with each entry replaced by its lowered Task<T>-returning
// coroutine via lower_function. When `with_line_directives` is set,
// `#line` markers pointing back to `filename` are emitted after each
// lowered function so compiler diagnostics and debuggers reference the
// user's original source rather than this generated file. When
// `with_runtime_include` is set and the file has at least one async
// function, `#include <asyncrt/asyncrt.hpp>` is prepended so the source
// doesn't need to include the runtime itself.
std::variant<std::string, ParseError> emit(const ParsedFile& parsed, const std::string& filename,
                                            bool with_line_directives, bool with_runtime_include = true);

} // namespace asynctp
