#include "asynctp/tokenizer.hpp"
#include "test_support.hpp"

#include <initializer_list>

using namespace asynctp;

namespace {

struct Expected {
  TokKind kind;
  std::string_view text;
};

const char* kind_name(TokKind k) {
  switch (k) {
    case TokKind::Identifier: return "Identifier";
    case TokKind::Number: return "Number";
    case TokKind::String: return "String";
    case TokKind::Char: return "Char";
    case TokKind::Punct: return "Punct";
    case TokKind::Directive: return "Directive";
    case TokKind::Comment: return "Comment";
    case TokKind::Invalid: return "Invalid";
  }
  return "?";
}

// Tokenizes `src` and asserts the exact token sequence. Each expected token's
// [begin,end) is pinned by locating its text in `src` at or after the end of
// the previous token, so both the kind and the exact range are checked.
void expect_tokens(std::string_view src, std::initializer_list<Expected> expected) {
  auto result = tokenize(src);
  if (std::holds_alternative<ParseError>(result)) {
    const auto& e = std::get<ParseError>(result);
    CHECK(false);
    std::cerr << "  unexpected error: " << e.message << " at " << e.line << ":" << e.col << "\n";
    return;
  }
  const auto& tf = std::get<TokenizedFile>(result);
  CHECK_EQ(tf.toks.size(), expected.size());

  std::size_t search_from = 0;
  std::size_t i = 0;
  for (const auto& exp : expected) {
    if (i >= tf.toks.size()) break;
    const Tok& tok = tf.toks[i];
    std::size_t want_begin = src.find(exp.text, search_from);
    bool ok = tok.kind == exp.kind && tok.begin == want_begin && tok.end == want_begin + exp.text.size();
    CHECK(ok);
    if (!ok) {
      std::cerr << "  token " << i << ": got " << kind_name(tok.kind) << " [" << tok.begin << "," << tok.end
                << ") '" << text(tf, tok) << "', want " << kind_name(exp.kind) << " '" << exp.text << "'\n";
    }
    search_from = tok.end;
    ++i;
  }
}

void expect_error(std::string_view src, std::size_t line, std::size_t col, std::string_view message) {
  auto result = tokenize(src);
  CHECK(std::holds_alternative<ParseError>(result));
  if (!std::holds_alternative<ParseError>(result)) return;
  const auto& e = std::get<ParseError>(result);
  CHECK_EQ(e.line, line);
  CHECK_EQ(e.col, col);
  CHECK_EQ(e.message, message);
}

void test_digit_separated_integer_is_one_number() {
  expect_tokens("1'000'000", {{TokKind::Number, "1'000'000"}});
}

void test_hex_with_separator_and_suffix_is_one_number() {
  expect_tokens("0x1'Fu", {{TokKind::Number, "0x1'Fu"}});
}

void test_float_with_signed_exponent_is_one_number() {
  expect_tokens("1.5e+3f", {{TokKind::Number, "1.5e+3f"}});
}

void test_leading_dot_float_is_one_number() {
  expect_tokens(".5", {{TokKind::Number, ".5"}});
}

void test_number_in_context() {
  expect_tokens("x = 1'000;", {{TokKind::Identifier, "x"},
                               {TokKind::Punct, "="},
                               {TokKind::Number, "1'000"},
                               {TokKind::Punct, ";"}});
}

void test_u8_string_is_one_string() {
  expect_tokens("u8\"a\"", {{TokKind::String, "u8\"a\""}});
}

void test_wide_char_is_one_char() {
  expect_tokens("L'x'", {{TokKind::Char, "L'x'"}});
}

void test_ud_suffix_string_is_one_string() {
  expect_tokens("\"s\"_sv", {{TokKind::String, "\"s\"_sv"}});
}

void test_raw_string_with_delimiter_is_one_string() {
  expect_tokens(R"--(R"x(a)"b)x")--", {{TokKind::String, R"--(R"x(a)"b)x")--"}});
}

void test_prefixed_raw_string_spanning_lines() {
  expect_tokens("u8R\"(a\nb)\" x", {{TokKind::String, "u8R\"(a\nb)\""}, {TokKind::Identifier, "x"}});
}

void test_string_escapes() {
  expect_tokens(R"("a\"b" '\'' c)", {{TokKind::String, R"("a\"b")"},
                                     {TokKind::Char, R"('\'')"},
                                     {TokKind::Identifier, "c"}});
}

void test_identifier_ending_in_prefix_letter_is_not_a_prefix() {
  expect_tokens("FOOR\"x\"", {{TokKind::Identifier, "FOOR"}, {TokKind::String, "\"x\""}});
}

void test_define_with_apostrophe_is_one_directive() {
  expect_tokens("#define A don't\nint x;", {{TokKind::Directive, "#define A don't"},
                                            {TokKind::Identifier, "int"},
                                            {TokKind::Identifier, "x"},
                                            {TokKind::Punct, ";"}});
}

void test_if0_prose_apostrophe_is_invalid_not_error() {
  // `isn` is an ordinary identifier; the lone quote becomes an Invalid
  // token running to the end of its line, and lexing carries on.
  expect_tokens("#if 0\nthis isn't\n#endif", {{TokKind::Directive, "#if 0"},
                                              {TokKind::Identifier, "this"},
                                              {TokKind::Identifier, "isn"},
                                              {TokKind::Invalid, "'t"},
                                              {TokKind::Directive, "#endif"}});
}

void test_unterminated_string_at_eof_is_invalid() {
  expect_tokens("x \"abc", {{TokKind::Identifier, "x"}, {TokKind::Invalid, "\"abc"}});
}

void test_multiline_define_is_one_directive() {
  expect_tokens("#define F(x) \\\n  x", {{TokKind::Directive, "#define F(x) \\\n  x"}});
}

void test_directive_with_block_comment_spanning_lines() {
  expect_tokens("#define A 1 /* a\n b */ + 2\nx", {{TokKind::Directive, "#define A 1 /* a\n b */ + 2"},
                                                   {TokKind::Identifier, "x"}});
}

void test_directive_line_comment_ends_it() {
  expect_tokens("#define A 1 // it's\nx", {{TokKind::Directive, "#define A 1 // it's"},
                                           {TokKind::Identifier, "x"}});
}

void test_directive_with_multiline_raw_string_is_one_directive() {
  // QA: GCC keeps a multi-line raw string inside its #define; the directive
  // must not end at the first physical newline inside the literal.
  expect_tokens("#define D R\"(\nasync\n)\"\nx", {{TokKind::Directive, "#define D R\"(\nasync\n)\""},
                                                  {TokKind::Identifier, "x"}});
}

void test_lone_quote_in_directive_swallows_block_comment_opener() {
  // QA: matches GCC (g++ -E): the unterminated `'` runs to end of line, so
  // the `/*` after it opens no comment and the next line is code.
  expect_tokens("#define M don't /* x\ny */ z", {{TokKind::Directive, "#define M don't /* x"},
                                                 {TokKind::Identifier, "y"},
                                                 {TokKind::Punct, "*"},
                                                 {TokKind::Punct, "/"},
                                                 {TokKind::Identifier, "z"}});
}

void test_comment_before_hash_keeps_directive() {
  // QA: a comment before `#` is whitespace; the line is still a directive.
  expect_tokens("/* c */ # define Y async\nx", {{TokKind::Comment, "/* c */"},
                                               {TokKind::Directive, "# define Y async"},
                                               {TokKind::Identifier, "x"}});
}

void test_unterminated_raw_string_in_error_directive_matches_gcc_position() {
  // QA: GCC rejects this too (even inside `#if 0`), at the same column.
  expect_error("#if 0\n#error prefer R\"strings over escapes\n#endif\n", 2, 15,
               "unterminated raw string literal");
}

void test_directive_with_crlf_splice() {
  expect_tokens("#define A \\\r\n  x\r\ny", {{TokKind::Directive, "#define A \\\r\n  x\r"},
                                             {TokKind::Identifier, "y"}});
}

void test_directive_after_leading_whitespace() {
  expect_tokens("  #  include <a.h>\nx", {{TokKind::Directive, "#  include <a.h>"},
                                          {TokKind::Identifier, "x"}});
}

void test_hash_not_first_on_line_is_punct() {
  expect_tokens("x # y", {{TokKind::Identifier, "x"}, {TokKind::Punct, "#"}, {TokKind::Identifier, "y"}});
}

void test_scope_arrow_ellipsis_are_single_puncts() {
  expect_tokens("a::b->c...", {{TokKind::Identifier, "a"},
                               {TokKind::Punct, "::"},
                               {TokKind::Identifier, "b"},
                               {TokKind::Punct, "->"},
                               {TokKind::Identifier, "c"},
                               {TokKind::Punct, "..."}});
}

void test_member_pointer_puncts() {
  expect_tokens("a.*b->*c", {{TokKind::Identifier, "a"},
                             {TokKind::Punct, ".*"},
                             {TokKind::Identifier, "b"},
                             {TokKind::Punct, "->*"},
                             {TokKind::Identifier, "c"}});
}

void test_shift_stays_split() {
  expect_tokens("x>>y", {{TokKind::Identifier, "x"},
                         {TokKind::Punct, ">"},
                         {TokKind::Punct, ">"},
                         {TokKind::Identifier, "y"}});
}

void test_dot_await_is_punct_then_identifier() {
  expect_tokens("f().await", {{TokKind::Identifier, "f"},
                              {TokKind::Punct, "("},
                              {TokKind::Punct, ")"},
                              {TokKind::Punct, "."},
                              {TokKind::Identifier, "await"}});
}

void test_comments() {
  expect_tokens("a // x \\\n still comment\n/* b\n */ c", {{TokKind::Identifier, "a"},
                                                           {TokKind::Comment, "// x \\\n still comment"},
                                                           {TokKind::Comment, "/* b\n */"},
                                                           {TokKind::Identifier, "c"}});
}

void test_unterminated_block_comment_is_error_at_opening() {
  expect_error("int a;\n  /* never closes", 2, 3, "unterminated block comment");
}

void test_unterminated_raw_string_is_error_at_opening() {
  expect_error("x\n y = R\"d(abc\n)\";", 2, 6, "unterminated raw string literal");
}

void test_unterminated_raw_string_in_directive_is_error_at_opening() {
  // Reported at the prefix, like the code-level case, not as a comment error.
  expect_error("x\n#define D u8R\"d(abc\n)\"\n", 2, 11, "unterminated raw string literal");
}

void test_directive_with_raw_prefix_identifier_not_raw() {
  // `R` followed by a space is an identifier then an ordinary string.
  expect_tokens("#define D R \"a\nx", {{TokKind::Directive, "#define D R \"a"}, {TokKind::Identifier, "x"}});
}

void test_non_ascii_identifier_is_one_token() {
  expect_tokens("int café = 1;", {{TokKind::Identifier, "int"},
                                   {TokKind::Identifier, "café"},
                                   {TokKind::Punct, "="},
                                   {TokKind::Number, "1"},
                                   {TokKind::Punct, ";"}});
}

void test_dollar_identifier() {
  expect_tokens("$x1", {{TokKind::Identifier, "$x1"}});
}

void test_location_of() {
  auto result = tokenize("ab\ncd\n\nx");
  CHECK(std::holds_alternative<TokenizedFile>(result));
  if (!std::holds_alternative<TokenizedFile>(result)) return;
  const auto& tf = std::get<TokenizedFile>(result);
  auto at = [&](std::size_t off, std::size_t line, std::size_t col) {
    auto loc = location_of(tf, off);
    CHECK_EQ(loc.line, line);
    CHECK_EQ(loc.col, col);
  };
  at(0, 1, 1);
  at(2, 1, 3); // the '\n' itself belongs to line 1
  at(3, 2, 1);
  at(6, 3, 1);
  at(7, 4, 1);
  at(8, 4, 2);   // one past the end
  at(100, 4, 2); // clamped
}

void test_empty_source() {
  auto result = tokenize("");
  CHECK(std::holds_alternative<TokenizedFile>(result));
  if (std::holds_alternative<TokenizedFile>(result)) {
    CHECK(std::get<TokenizedFile>(result).toks.empty());
  }
}

} // namespace

int main() {
  test_digit_separated_integer_is_one_number();
  test_hex_with_separator_and_suffix_is_one_number();
  test_float_with_signed_exponent_is_one_number();
  test_leading_dot_float_is_one_number();
  test_number_in_context();
  test_u8_string_is_one_string();
  test_wide_char_is_one_char();
  test_ud_suffix_string_is_one_string();
  test_raw_string_with_delimiter_is_one_string();
  test_prefixed_raw_string_spanning_lines();
  test_string_escapes();
  test_identifier_ending_in_prefix_letter_is_not_a_prefix();
  test_define_with_apostrophe_is_one_directive();
  test_if0_prose_apostrophe_is_invalid_not_error();
  test_unterminated_string_at_eof_is_invalid();
  test_multiline_define_is_one_directive();
  test_directive_with_block_comment_spanning_lines();
  test_directive_line_comment_ends_it();
  test_directive_with_multiline_raw_string_is_one_directive();
  test_lone_quote_in_directive_swallows_block_comment_opener();
  test_comment_before_hash_keeps_directive();
  test_unterminated_raw_string_in_error_directive_matches_gcc_position();
  test_directive_with_crlf_splice();
  test_directive_after_leading_whitespace();
  test_hash_not_first_on_line_is_punct();
  test_scope_arrow_ellipsis_are_single_puncts();
  test_member_pointer_puncts();
  test_shift_stays_split();
  test_dot_await_is_punct_then_identifier();
  test_comments();
  test_unterminated_block_comment_is_error_at_opening();
  test_unterminated_raw_string_is_error_at_opening();
  test_unterminated_raw_string_in_directive_is_error_at_opening();
  test_directive_with_raw_prefix_identifier_not_raw();
  test_non_ascii_identifier_is_one_token();
  test_dollar_identifier();
  test_location_of();
  test_empty_source();
  return asynctp::test::report("tokenizer_tests");
}
