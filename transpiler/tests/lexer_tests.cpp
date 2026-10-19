#include "asynctp/lexer.hpp"
#include "asynctp/scanner.hpp"
#include "asynctp/tokenizer.hpp"
#include "test_support.hpp"

using namespace asynctp;

namespace {

void test_skip_whitespace_and_comments() {
  SourceCursor cur("   // line comment\n/* block \n comment */  x");
  cur.skip_whitespace_and_comments();
  CHECK(!cur.has_error());
  CHECK_EQ(cur.peek(), 'x');
}

void test_unterminated_block_comment() {
  SourceCursor cur("/* never closes");
  cur.skip_whitespace_and_comments();
  CHECK(cur.has_error());
}

void test_skip_string_literal_with_escapes() {
  SourceCursor cur(R"("hello \" world" rest)");
  cur.skip_string_or_char_literal();
  CHECK(!cur.has_error());
  CHECK_EQ(cur.peek(), ' ');
}

void test_skip_char_literal() {
  SourceCursor cur(R"('\'' rest)");
  cur.skip_string_or_char_literal();
  CHECK(!cur.has_error());
  CHECK_EQ(cur.peek(), ' ');
}

void test_skip_raw_string_literal() {
  // R"(contains "quotes" and ) parens)" then trailing text.
  // Precondition of skip_string_or_char_literal is that the cursor sits AT
  // the opening quote (the 'R' prefix, like in real usage, is consumed by
  // the caller beforehand), so advance past it here first.
  std::string_view src = R"TEST(R"(contains "quotes" and ) parens)" rest)TEST";
  SourceCursor cur(src);
  cur.advance_one(); // consume 'R'
  cur.skip_string_or_char_literal();
  CHECK(!cur.has_error());
  CHECK_EQ(cur.peek(), ' ');
}

void test_skip_raw_string_with_custom_delimiter() {
  std::string_view src = R"(R"delim(has ) and " inside)delim" rest)";
  SourceCursor cur(src);
  cur.advance_one(); // consume 'R'
  cur.skip_string_or_char_literal();
  CHECK(!cur.has_error());
  CHECK_EQ(cur.peek(), ' ');
}

void test_unterminated_string_literal() {
  SourceCursor cur("\"never closes");
  cur.skip_string_or_char_literal();
  CHECK(cur.has_error());
}

void test_find_matching_simple() {
  std::string_view src = "(abc)";
  SourceCursor cur(src);
  cur.advance_one(); // consume opening '('
  auto close = cur.find_matching('(', ')');
  CHECK(close.has_value());
  CHECK_EQ(*close, src.size() - 1);
}

void test_find_matching_nested() {
  std::string_view src = "(a(b)c(d(e)f)g)";
  SourceCursor cur(src);
  cur.advance_one();
  auto close = cur.find_matching('(', ')');
  CHECK(close.has_value());
  CHECK_EQ(*close, src.size() - 1);
}

void test_find_matching_skips_strings_and_comments() {
  std::string_view src = "(a \"not)a)close\" /* )also not */ b)";
  SourceCursor cur(src);
  cur.advance_one();
  auto close = cur.find_matching('(', ')');
  CHECK(close.has_value());
  CHECK_EQ(*close, src.size() - 1);
}

void test_find_matching_unterminated() {
  std::string_view src = "(a(b)c";
  SourceCursor cur(src);
  cur.advance_one();
  auto close = cur.find_matching('(', ')');
  CHECK(!close.has_value());
  CHECK(cur.has_error());
}

void test_scan_identifier() {
  SourceCursor cur("_foo123 bar");
  auto id = cur.scan_identifier_if_present();
  CHECK_EQ(id, "_foo123");
  CHECK_EQ(cur.peek(), ' ');
}

void test_scan_identifier_absent() {
  SourceCursor cur("123abc");
  auto id = cur.scan_identifier_if_present();
  CHECK(id.empty());
}

// Tokenizes `src` and returns its markers; a tokenizer error fails the check
// and yields no markers.
std::vector<std::size_t> markers_of(std::string_view src) {
  auto tf = tokenize(src);
  CHECK(std::holds_alternative<TokenizedFile>(tf));
  if (!std::holds_alternative<TokenizedFile>(tf)) return {};
  return find_async_markers(std::get<TokenizedFile>(tf));
}

void test_find_async_markers_basic() {
  auto markers = markers_of("int x; async int foo() { return 1; }");
  CHECK_EQ(markers.size(), 1u);
}

void test_find_async_markers_ignores_strings_and_comments() {
  auto markers = markers_of(
      "const char* s = \"async nope\"; // async also nope\n"
      "/* async still nope */ async int real() { return 0; }");
  CHECK_EQ(markers.size(), 1u);
}

void test_find_async_markers_multiple() {
  auto markers = markers_of(
      "async int a() { return 1; } int plain() { return 2; } async int b() { return 3; }");
  CHECK_EQ(markers.size(), 2u);
}

void test_find_async_markers_known_limitation_identifier_named_async() {
  // Documented MVP limitation: a variable literally named `async` is
  // misdetected as a marker. This test pins down that known behavior so a
  // future change to it is a deliberate decision, not an accident.
  auto markers = markers_of("int async = 5;");
  CHECK_EQ(markers.size(), 1u);
}

void test_find_async_markers_skips_directives() {
  std::string_view src = "#define X async\nasync int f() {}";
  auto markers = markers_of(src);
  CHECK_EQ(markers.size(), 1u);
  if (markers.size() == 1) CHECK_EQ(markers[0], src.find("async int"));
}

void test_find_async_markers_survives_digit_separator() {
  // Used to stop at the `'` and silently drop every later marker.
  std::string_view src = "int n = 1'000;\nasync int f() {}";
  auto markers = markers_of(src);
  CHECK_EQ(markers.size(), 1u);
  if (markers.size() == 1) CHECK_EQ(markers[0], src.find("async"));
}

void test_find_async_markers_whole_identifier_only() {
  // `asyncé` is one identifier, not `async` followed by stray bytes.
  auto markers = markers_of("int asyncé = 1; int async_x = 2; int my_async = 3;");
  CHECK_EQ(markers.size(), 0u);
}

} // namespace

int main() {
  test_skip_whitespace_and_comments();
  test_unterminated_block_comment();
  test_skip_string_literal_with_escapes();
  test_skip_char_literal();
  test_skip_raw_string_literal();
  test_skip_raw_string_with_custom_delimiter();
  test_unterminated_string_literal();
  test_find_matching_simple();
  test_find_matching_nested();
  test_find_matching_skips_strings_and_comments();
  test_find_matching_unterminated();
  test_scan_identifier();
  test_scan_identifier_absent();
  test_find_async_markers_basic();
  test_find_async_markers_ignores_strings_and_comments();
  test_find_async_markers_multiple();
  test_find_async_markers_known_limitation_identifier_named_async();
  test_find_async_markers_skips_directives();
  test_find_async_markers_survives_digit_separator();
  test_find_async_markers_whole_identifier_only();
  return asynctp::test::report("lexer_tests");
}
