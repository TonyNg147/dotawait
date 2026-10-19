#pragma once

#include "tokenizer.hpp"

#include <cstddef>
#include <vector>

namespace asynctp {

// Walks the token stream of the whole file and records the begin offset of
// every Identifier token whose text is exactly "async". Literals, comments
// and preprocessor directives are single opaque tokens (tokenizer.hpp), so
// an `async` inside a string, a comment, or a `#define` is never a marker.
// Ordinary C++ content is never semantically descended into here; the
// parser (async_parser.hpp) takes it from here, starting from each marker.
//
// Known MVP limitation: "async" is treated as a reserved marker word
// recognized at any identifier position, not just where a real
// declaration could start. A variable or function literally named `async`
// will be misdetected as a marker.
std::vector<std::size_t> find_async_markers(const TokenizedFile& file);

} // namespace asynctp
