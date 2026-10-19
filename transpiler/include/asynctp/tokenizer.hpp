#pragma once

#include "diagnostics.hpp"

#include <cstddef>
#include <string_view>
#include <variant>
#include <vector>

namespace asynctp {

enum class TokKind { Identifier, Number, String, Char, Punct, Directive, Comment, Invalid };

// One token as a [begin, end) byte range into TokenizedFile::source. Tokens
// never own text; whitespace is not a token (the gaps between tokens are).
struct Tok {
  TokKind kind;
  std::size_t begin;
  std::size_t end;
};

struct TokenizedFile {
  std::string_view source;
  std::vector<Tok> toks;
  std::vector<std::size_t> line_starts; // offset of the first byte of each line
};

// Splits the whole buffer into a flat token vector in one pass. This is the
// single place that knows how to step over literals, comments and
// preprocessor lines; everything above it works on token indices.
//
//   Identifier  [A-Za-z_$] or any byte >= 0x80, then those or digits (so
//               UTF-8 identifiers stay one token).
//   Number      a pp-number: `1'000'000`, `0x1'Fu`, `1.5e+3f`, `.5`.
//   String/Char optional u8/u/U/L prefix, escapes, optional ud-suffix
//               (`"s"_sv`); raw strings `R"d(...)d"` with any R-prefix.
//   Directive   `#` first on a logical line, to the end of that line,
//               following backslash-newline and any `/* */` or raw string
//               that starts in it (either may span lines). Ordinary quotes
//               inside are lexed leniently and never error.
//   Comment     `//` (honouring backslash-newline) and `/* */`.
//   Punct       one character, except `::` `->` `...` `.*` `->*`. `>>`,
//               `<<`, `<=`, `>=` stay split so `>` can close a template.
//
// Invalid-token policy: an ordinary string/char literal that hits a newline
// or EOF before its closing quote becomes an Invalid token from the quote to
// the end of the line, NOT an error. GCC accepts such lone quotes with only a
// warning (`#if 0` prose like "isn't", `#define MSG don't`), and every
// project file passes through asynctp, so erroring here would reject code
// that builds. The parser instead errors only when an Invalid token falls
// inside an async island, where it would otherwise corrupt the lowering.
//
// Hard errors (GCC rejects these too), reported at the opening delimiter:
// an unterminated `/*` comment and an unterminated raw string, including
// either one starting inside a directive.
//
// Not a full C++ lexer: no trigraphs, no digraphs (`<:` lexes as `<`, `:`),
// and no backslash-newline splicing outside directives, `//` comments and
// literals.
std::variant<TokenizedFile, ParseError> tokenize(std::string_view source);

// 1-based line/col of a byte offset (clamped to the end of the source),
// by binary search on line_starts.
SourceLocation location_of(const TokenizedFile& file, std::size_t offset);

inline std::string_view text(const TokenizedFile& file, const Tok& tok) {
  return file.source.substr(tok.begin, tok.end - tok.begin);
}

} // namespace asynctp
