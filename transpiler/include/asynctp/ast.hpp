#pragma once

#include "tokenizer.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace asynctp {

// One `async`-marked function found in a source file. Every field other than
// the offsets is a verbatim view into the original source buffer -- nothing
// here is reformatted or re-parsed beyond what the function-name heuristic
// needs (see async_parser.hpp for the exact algorithm).
struct AsyncFunction {
  std::size_t async_kw_offset;   // offset of the leading "async" keyword
  std::string_view return_type;  // verbatim, trimmed text between "async" and the name
  std::string_view name;
  std::string_view params;       // verbatim, including the enclosing '(' and ')'
  std::size_t body_open_offset;  // offset just after the body's opening '{'
  std::size_t body_close_offset; // offset of the body's matching '}'
  std::string_view body;         // verbatim text strictly between the body braces
};

// A parsed file is a flat list of AsyncFunction "islands" in source order.
// Everything NOT covered by some AsyncFunction's span
// [async_kw_offset, body_close_offset] is implicit ordinary C++ passthrough
// text -- it is never represented structurally, only as the gaps between
// consecutive functions (and before the first / after the last).
struct ParsedFile {
  std::string_view source;
  std::vector<AsyncFunction> functions;
  TokenizedFile tokens; // the whole-file token stream parse_file built, for later stages to reuse
};

} // namespace asynctp
