#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace asynctp {

// Flatly walks the whole file once, skipping over string/char literals and
// comments (so an `async` appearing inside either is correctly ignored), and
// records the byte offset of every identifier token whose text is exactly
// "async". Ordinary C++ content (including nested braces of unrelated
// functions/classes/namespaces) is never semantically descended into here —
// it is simply walked over one token at a time. The parser (async_parser.hpp)
// takes it from here, starting from each marker offset.
//
// Known MVP limitation: "async" is treated as a reserved marker word
// recognized at any top-level identifier position, not just where a real
// declaration could start. A variable or function literally named `async`
// will be misdetected as a marker.
std::vector<std::size_t> find_async_markers(std::string_view source);

} // namespace asynctp
