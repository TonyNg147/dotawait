#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace asynctp::lsp {

// LSP base-protocol framing: `Content-Length: N\r\n\r\n<N bytes of JSON>`.
//
// Streams must be in binary mode: N counts bytes, and the header lines must
// arrive as literal `\r\n`. On POSIX `std::cin`/`std::cout` already are.
// Windows (where they translate line endings in text mode) is out of scope.

// Reads one message and returns its body bytes (not parsed as JSON).
// Headers are read up to the empty `\r\n` line; `Content-Length` (name
// matched case-insensitively) is required and must appear once, every other
// header (e.g. `Content-Type`) is ignored. Then exactly N body bytes are read.
//
// Returns nullopt both at a clean end of stream (no byte of a next message
// was read) and on error; `*error` tells them apart: it is cleared on success
// and on clean EOF, and set to a message for a malformed header, a header
// line not ending in `\r\n`, a missing/invalid length, EOF inside the header
// block, or a body shorter than N. `error` may be nullptr. After an error the
// stream position is unspecified, so stop reading from it.
std::optional<std::string> read_message(std::istream& in, std::string* error);

// Writes `Content-Length: <json.size()>\r\n\r\n` followed by `json`, then
// flushes so the peer sees the whole message immediately.
void write_message(std::ostream& out, std::string_view json);

} // namespace asynctp::lsp
