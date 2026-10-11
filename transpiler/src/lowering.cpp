#include "asynctp/lowering.hpp"

#include "asynctp/scanner.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace asynctp {

namespace {

ParseError error_from(const ScanError& e) { return ParseError{e.offset, e.line, e.col, e.message}; }

// Looks backward from `bracket_offset` (the position of a '[') in `body` to
// decide whether it begins a lambda capture list (expression-start context)
// or is an ordinary subscript operator on a preceding identifier/call-result.
// See lowering.hpp for the documented limitation on keyword coverage.
bool looks_like_lambda_start(std::string_view body, std::size_t bracket_offset) {
  std::size_t i = bracket_offset;
  while (i > 0 && std::isspace(static_cast<unsigned char>(body[i - 1]))) i--;
  if (i == 0) return true;

  char prev = body[i - 1];
  if (prev == ')' || prev == ']') return false; // call-result / subscript-result -> subscript

  if (!(std::isalnum(static_cast<unsigned char>(prev)) || prev == '_')) {
    return true; // preceded by punctuation/operator -> expression-start context
  }

  std::size_t word_end = i;
  std::size_t word_start = word_end;
  while (word_start > 0 &&
         (std::isalnum(static_cast<unsigned char>(body[word_start - 1])) || body[word_start - 1] == '_')) {
    word_start--;
  }
  std::string_view word = body.substr(word_start, word_end - word_start);
  return word == "return" || word == "throw" || word == "co_return";
}

// Backward, best-effort postfix-expression scan from just before a `.await`
// at `dot_offset`, stopping no earlier than `min_offset`. Greedily consumes
// identifier/`.`/`::`/`->` chains and balanced `(...)`/`[...]` groups.
std::size_t find_expression_start_backward(std::string_view body, std::size_t dot_offset, std::size_t min_offset) {
  std::size_t i = dot_offset;
  while (i > min_offset) {
    // Remember where we were before skipping separating whitespace, so that
    // if this iteration turns out NOT to extend the expression, that
    // whitespace is restored as part of the untouched prefix rather than
    // being silently swallowed (e.g. the space in "x = helper(x).await").
    std::size_t i_before_ws = i;
    while (i > min_offset && std::isspace(static_cast<unsigned char>(body[i - 1]))) i--;
    if (i <= min_offset) {
      i = i_before_ws;
      break;
    }

    char c = body[i - 1];

    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
      while (i > min_offset && (std::isalnum(static_cast<unsigned char>(body[i - 1])) || body[i - 1] == '_')) i--;
      continue;
    }
    if (c == '.') {
      i--;
      continue;
    }
    if (c == ':' && i >= min_offset + 2 && body[i - 2] == ':') {
      i -= 2;
      continue;
    }
    if (c == '>' && i >= min_offset + 2 && body[i - 2] == '-') {
      i -= 2; // '->'
      continue;
    }
    if (c == ')' || c == ']') {
      char open_ch = (c == ')') ? '(' : '[';
      char close_ch = c;
      int depth = 1;
      std::size_t j = i - 1;
      while (j > min_offset && depth > 0) {
        j--;
        if (body[j] == close_ch) depth++;
        else if (body[j] == open_ch) depth--;
      }
      if (depth != 0) {
        i = i_before_ws; // unbalanced -- stop without consuming
        break;
      }
      i = j;
      continue;
    }

    i = i_before_ws; // this character doesn't extend the expression
    break;
  }
  return i;
}

} // namespace

std::variant<std::string, ParseError> lower_body(std::string_view body) {
  std::string out;
  out.reserve(body.size() + 32);

  SourceCursor cur(body);
  std::size_t last_copied = 0;
  std::vector<bool> frame_is_lambda;
  int lambda_depth = 0;
  bool at_statement_start = true;
  // Nesting of ()/[] groups, and the end of the most recent comment seen
  // outside any group. The backward `.await` scan never crosses that point,
  // so a comment like "// ...give other tasks a turn." on the line above
  // isn't mistaken for part of the awaited expression.
  int group_depth = 0;
  std::size_t comment_floor = 0;

  auto copy_verbatim_to = [&](std::size_t end_offset) { out.append(body.substr(last_copied, end_offset - last_copied)); last_copied = end_offset; };

  while (true) {
    std::size_t gap_start = cur.offset();
    cur.skip_whitespace_and_comments();
    if (cur.has_error()) return error_from(cur.error());
    if (group_depth == 0) {
      std::size_t gap_end = cur.offset();
      while (gap_end > gap_start && std::isspace(static_cast<unsigned char>(body[gap_end - 1]))) gap_end--;
      if (gap_end > gap_start) comment_floor = gap_end; // the gap held a comment
    }
    if (cur.at_end()) break;

    char c = cur.peek();

    if (c == '"' || c == '\'') {
      cur.skip_string_or_char_literal();
      if (cur.has_error()) return error_from(cur.error());
      at_statement_start = false;
      continue;
    }

    if (c == ';') {
      cur.advance_one();
      at_statement_start = true;
      continue;
    }

    if (c == '{') {
      cur.advance_one();
      frame_is_lambda.push_back(false);
      at_statement_start = true;
      continue;
    }

    if (c == '}') {
      cur.advance_one();
      if (!frame_is_lambda.empty()) {
        if (frame_is_lambda.back()) lambda_depth--;
        frame_is_lambda.pop_back();
      }
      at_statement_start = true;
      continue;
    }

    if (c == '(') {
      cur.advance_one();
      group_depth++;
      at_statement_start = false;
      continue;
    }

    if (c == ')' || c == ']') {
      cur.advance_one();
      if (group_depth > 0) group_depth--;
      at_statement_start = false;
      continue;
    }

    if (c == '[') {
      std::size_t bracket_offset = cur.offset();
      if (!looks_like_lambda_start(body, bracket_offset)) {
        cur.advance_one();
        group_depth++;
        at_statement_start = false;
        continue;
      }

      cur.advance_one(); // consume '['
      auto capture_close = cur.find_matching('[', ']');
      if (!capture_close.has_value()) return error_from(cur.error());

      // Skip the rest of the lambda header (optional params, mutable,
      // noexcept(...), trailing return type, attributes) up to the body's
      // opening '{', balancing any further ()/[] encountered along the way.
      while (true) {
        cur.skip_whitespace_and_comments();
        if (cur.has_error()) return error_from(cur.error());
        if (cur.at_end()) {
          auto loc = cur.location_at(bracket_offset);
          return ParseError{bracket_offset, loc.line, loc.col,
                             "unterminated lambda expression (no '{' found after capture list)"};
        }
        char cc = cur.peek();
        if (cc == '{') break;
        if (cc == '(') {
          cur.advance_one();
          if (!cur.find_matching('(', ')').has_value()) return error_from(cur.error());
          continue;
        }
        if (cc == '[') {
          cur.advance_one();
          if (!cur.find_matching('[', ']').has_value()) return error_from(cur.error());
          continue;
        }
        if (cc == '"' || cc == '\'') {
          cur.skip_string_or_char_literal();
          if (cur.has_error()) return error_from(cur.error());
          continue;
        }
        cur.advance_one();
      }

      cur.advance_one(); // consume the lambda body's '{'
      frame_is_lambda.push_back(true);
      lambda_depth++;
      at_statement_start = true;
      continue;
    }

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      std::size_t id_start = cur.offset();
      std::string_view id = cur.scan_identifier_if_present();

      if (lambda_depth == 0 && at_statement_start && id == "return") {
        copy_verbatim_to(id_start);
        out.append("co_return");
        last_copied = cur.offset();
      }
      at_statement_start = false;
      continue;
    }

    if (c == '.') {
      std::size_t dot_offset = cur.offset();
      SourceCursor probe = cur;
      probe.advance_one(); // consume '.'
      probe.skip_whitespace_and_comments();
      if (probe.has_error()) return error_from(probe.error());
      std::string_view maybe_await = probe.scan_identifier_if_present();

      if (lambda_depth == 0 && maybe_await == "await" && probe.peek() != '(') {
        std::size_t expr_start = find_expression_start_backward(body, dot_offset, std::max(last_copied, comment_floor));
        copy_verbatim_to(expr_start);
        out.append("co_await (");
        out.append(body.substr(expr_start, dot_offset - expr_start));
        out.append(")");
        last_copied = probe.offset();
        cur = probe;
        at_statement_start = false;
        continue;
      }

      cur.advance_one(); // just an ordinary '.', not a rewritten .await
      at_statement_start = false;
      continue;
    }

    cur.advance_one();
    at_statement_start = false;
  }

  copy_verbatim_to(body.size());
  return out;
}

std::variant<std::string, ParseError> lower_function(const AsyncFunction& fn) {
  auto lowered = lower_body(fn.body);
  if (std::holds_alternative<ParseError>(lowered)) return std::get<ParseError>(lowered);

  std::string out;
  out.reserve(fn.return_type.size() + fn.name.size() + fn.params.size() + std::get<std::string>(lowered).size() + 32);
  out += "::asyncrt::Task<";
  out += fn.return_type;
  out += "> ";
  out += fn.name;
  out += fn.params;
  out += " {";
  out += std::get<std::string>(lowered);
  out += "}";
  return out;
}

} // namespace asynctp
