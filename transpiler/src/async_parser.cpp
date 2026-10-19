#include "asynctp/async_parser.hpp"

#include "asynctp/lexer.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace asynctp {

namespace {

std::string_view trim_view(std::string_view s) {
  std::size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
  std::size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
  return s.substr(start, end - start);
}

ParseError error_at(const TokenizedFile& tf, std::size_t offset, std::string message) {
  auto loc = location_of(tf, offset);
  return ParseError{offset, loc.line, loc.col, std::move(message)};
}

ParseError unterminated_literal(const TokenizedFile& tf, const Tok& tok) {
  return error_at(tf, tok.begin, "unterminated string or character literal");
}

bool is_punct(const TokenizedFile& tf, const Tok& tok, std::string_view p) {
  return tok.kind == TokKind::Punct && text(tf, tok) == p;
}

// Index of the first non-Comment token at or after `i` (toks.size() if none).
// Comments are skipped like whitespace; directives are not, matching the
// "immediately followed by" of the heuristic.
std::size_t skip_comments(const TokenizedFile& tf, std::size_t i) {
  while (i < tf.toks.size() && tf.toks[i].kind == TokKind::Comment) i++;
  return i;
}

// Outcome of balancing from an opening Punct token.
struct Match {
  std::optional<std::size_t> close; // index of the matching closer, nullopt at EOF
  std::optional<std::size_t> invalid; // index of an Invalid token met first
};

// Precondition: toks[open] is the Punct `open_p`. Only Punct tokens count
// toward depth; every other token is an atom.
Match find_matching(const TokenizedFile& tf, std::size_t open, std::string_view open_p, std::string_view close_p) {
  int depth = 0;
  for (std::size_t i = open; i < tf.toks.size(); ++i) {
    const Tok& tok = tf.toks[i];
    if (tok.kind == TokKind::Invalid) return Match{std::nullopt, i};
    if (is_punct(tf, tok, open_p)) {
      depth++;
    } else if (is_punct(tf, tok, close_p)) {
      if (--depth == 0) return Match{i, std::nullopt};
    }
  }
  return Match{};
}

// Applies the function-name heuristic starting just after the `async`
// token at index `marker`. See async_parser.hpp for the algorithm summary.
std::variant<AsyncFunction, ParseError> parse_one(const TokenizedFile& tf, std::size_t marker) {
  const auto& toks = tf.toks;
  std::string_view source = tf.source;
  std::size_t async_offset = toks[marker].begin;
  std::size_t return_type_start = toks[marker].end;

  for (std::size_t i = marker + 1; i < toks.size(); ++i) {
    const Tok& id = toks[i];
    if (id.kind == TokKind::Invalid) return unterminated_literal(tf, id);
    if (id.kind != TokKind::Identifier) continue;

    // Probe whether `id` is immediately followed by a balanced `(...)`
    // immediately followed by `{`; a failed guess just moves on.
    std::size_t open_paren = skip_comments(tf, i + 1);
    if (open_paren >= toks.size() || !is_punct(tf, toks[open_paren], "(")) continue;

    Match params = find_matching(tf, open_paren, "(", ")");
    if (params.invalid) return unterminated_literal(tf, toks[*params.invalid]);
    if (!params.close) continue; // this identifier wasn't it; keep scanning

    std::size_t open_brace = skip_comments(tf, *params.close + 1);
    if (open_brace >= toks.size() || !is_punct(tf, toks[open_brace], "{")) continue;

    Match body = find_matching(tf, open_brace, "{", "}");
    if (body.invalid) return unterminated_literal(tf, toks[*body.invalid]);
    if (!body.close) {
      return error_at(tf, source.size(), "unexpected end of file while matching delimiters");
    }

    std::size_t body_open_offset = toks[open_brace].end;
    std::size_t body_close_offset = toks[*body.close].begin;

    AsyncFunction fn;
    fn.async_kw_offset = async_offset;
    fn.return_type = trim_view(source.substr(return_type_start, id.begin - return_type_start));
    fn.name = text(tf, id);
    fn.params = source.substr(toks[open_paren].begin, toks[*params.close].end - toks[open_paren].begin);
    fn.body_open_offset = body_open_offset;
    fn.body_close_offset = body_close_offset;
    fn.body = source.substr(body_open_offset, body_close_offset - body_open_offset);
    return fn;
  }

  return error_at(tf, async_offset, "could not find a function name, parameter list, and body after 'async'");
}

// Index of the token beginning exactly at `offset` (a marker offset).
std::size_t token_index_at(const TokenizedFile& tf, std::size_t offset) {
  auto it = std::lower_bound(tf.toks.begin(), tf.toks.end(), offset,
                             [](const Tok& tok, std::size_t off) { return tok.begin < off; });
  return static_cast<std::size_t>(it - tf.toks.begin());
}

} // namespace

std::variant<ParsedFile, ParseError> parse_file(std::string_view source) {
  auto tokenized = tokenize(source);
  if (std::holds_alternative<ParseError>(tokenized)) {
    return std::get<ParseError>(std::move(tokenized));
  }

  ParsedFile result;
  result.source = source;
  result.tokens = std::get<TokenizedFile>(std::move(tokenized));
  const TokenizedFile& tf = result.tokens;

  std::size_t consumed_until = 0;
  for (std::size_t async_offset : find_async_markers(tf)) {
    if (async_offset < consumed_until) continue; // inside a function already extracted

    auto parsed = parse_one(tf, token_index_at(tf, async_offset));
    if (std::holds_alternative<ParseError>(parsed)) {
      return std::get<ParseError>(std::move(parsed));
    }

    AsyncFunction fn = std::get<AsyncFunction>(std::move(parsed));
    consumed_until = fn.body_close_offset + 1;
    result.functions.push_back(std::move(fn));
  }

  return result;
}

} // namespace asynctp
