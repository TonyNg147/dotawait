#include "asynctp/async_parser.hpp"
#include "test_support.hpp"

using namespace asynctp;

namespace {

AsyncFunction expect_single(std::string_view src) {
  auto parsed = parse_file(src);
  if (std::holds_alternative<ParseError>(parsed)) {
    const auto& e = std::get<ParseError>(parsed);
    CHECK(false);
    std::cerr << "  unexpected parse error: " << e.message << " at " << e.line << ":" << e.col << "\n";
    return {};
  }
  const auto& pf = std::get<ParsedFile>(parsed);
  CHECK_EQ(pf.functions.size(), 1u);
  return pf.functions.empty() ? AsyncFunction{} : pf.functions[0];
}

void test_simple_signature() {
  auto fn = expect_single("async int compute(int x) { return x * 2; }");
  CHECK_EQ(fn.return_type, "int");
  CHECK_EQ(fn.name, "compute");
  CHECK_EQ(fn.params, "(int x)");
  CHECK_EQ(fn.body, " return x * 2; ");
}

void test_templated_return_type() {
  auto fn = expect_single("async std::vector<int> make() { return {}; }");
  CHECK_EQ(fn.return_type, "std::vector<int>");
  CHECK_EQ(fn.name, "make");
}

void test_reference_return_type() {
  auto fn = expect_single("async const Foo& get(Bar b) { return b.foo; }");
  CHECK_EQ(fn.return_type, "const Foo&");
  CHECK_EQ(fn.name, "get");
  CHECK_EQ(fn.params, "(Bar b)");
}

void test_void_no_params() {
  auto fn = expect_single("async void run() { do_thing(); }");
  CHECK_EQ(fn.return_type, "void");
  CHECK_EQ(fn.name, "run");
  CHECK_EQ(fn.params, "()");
}

void test_default_arg_with_nested_parens() {
  auto fn = expect_single("async int f(int x = (1 + 2)) { return x; }");
  CHECK_EQ(fn.return_type, "int");
  CHECK_EQ(fn.params, "(int x = (1 + 2))");
}

void test_nested_braces_in_body() {
  auto fn = expect_single("async int f() { if (true) { return 1; } return 0; }");
  CHECK_EQ(fn.body, " if (true) { return 1; } return 0; ");
}

void test_multiple_functions_and_passthrough() {
  auto parsed = parse_file(
      "int helper() { return 1; } "
      "async int a() { return 1; } "
      "struct S { int x; }; "
      "async int b(int y) { return y; }");
  CHECK(std::holds_alternative<ParsedFile>(parsed));
  const auto& pf = std::get<ParsedFile>(parsed);
  CHECK_EQ(pf.functions.size(), 2u);
  if (pf.functions.size() == 2) {
    CHECK_EQ(pf.functions[0].name, "a");
    CHECK_EQ(pf.functions[1].name, "b");
  }
}

void test_string_and_comment_do_not_confuse_body_matching() {
  auto fn = expect_single(
      "async int f() { const char* s = \"} not a brace\"; /* } also not */ return 1; }");
  CHECK_EQ(fn.name, "f");
}

void test_known_limitation_function_pointer_return_type_errors() {
  // Documented MVP limitation: a raw function-pointer return type defeats
  // the heuristic. Pin down that this produces a clear ParseError rather
  // than silently mis-lowering.
  auto parsed = parse_file("async void (*make_handler())(int) { return nullptr; }");
  CHECK(std::holds_alternative<ParseError>(parsed));
}

void test_no_async_functions_is_empty_success() {
  auto parsed = parse_file("int plain() { return 1; }");
  CHECK(std::holds_alternative<ParsedFile>(parsed));
  if (std::holds_alternative<ParsedFile>(parsed)) {
    CHECK_EQ(std::get<ParsedFile>(parsed).functions.size(), 0u);
  }
}

void test_unterminated_body_is_error() {
  auto parsed = parse_file("async int f() { return 1;");
  CHECK(std::holds_alternative<ParseError>(parsed));
}

void test_digit_separator_before_async_function() {
  // Used to stop the marker scan at the `'`, silently leaving `f` unlowered.
  auto fn = expect_single("constexpr int kMax = 1'000;\nasync int f() { return kMax; }");
  CHECK_EQ(fn.name, "f");
  CHECK_EQ(fn.return_type, "int");
  CHECK_EQ(fn.body, " return kMax; ");
}

void test_apostrophe_in_if0_text_before_async_function() {
  auto fn = expect_single("#if 0\nthis isn't compiled\n#endif\nasync int f() { return 1; }");
  CHECK_EQ(fn.name, "f");
}

void test_apostrophe_in_define_before_async_function() {
  auto fn = expect_single("#define MSG don't\nasync int f() { return 1; }");
  CHECK_EQ(fn.name, "f");
}

void test_lone_quote_inside_async_body_is_error_at_quote() {
  // The Invalid token swallows the rest of the line (the `}` included), so
  // this must be reported at the quote, not as a generic "no body" error.
  auto parsed = parse_file("async int f() { auto s = 'x; return 1; }");
  CHECK(std::holds_alternative<ParseError>(parsed));
  if (std::holds_alternative<ParseError>(parsed)) {
    const auto& e = std::get<ParseError>(parsed);
    CHECK_EQ(e.line, 1u);
    CHECK_EQ(e.col, 26u);
    CHECK_EQ(e.offset, 25u);
    CHECK(e.message.find("unterminated") != std::string::npos);
  }
}

void test_lone_quote_in_return_type_is_error_at_quote() {
  auto parsed = parse_file("async int 'x\nf() { return 1; }");
  CHECK(std::holds_alternative<ParseError>(parsed));
  if (std::holds_alternative<ParseError>(parsed)) {
    const auto& e = std::get<ParseError>(parsed);
    CHECK_EQ(e.line, 1u);
    CHECK_EQ(e.col, 11u);
    CHECK(e.message.find("unterminated") != std::string::npos);
  }
}

void test_lone_quote_in_params_is_error_at_quote() {
  auto parsed = parse_file("async int f(char c = 'x) { return 1; }");
  CHECK(std::holds_alternative<ParseError>(parsed));
  if (std::holds_alternative<ParseError>(parsed)) {
    const auto& e = std::get<ParseError>(parsed);
    CHECK_EQ(e.line, 1u);
    CHECK_EQ(e.col, 22u);
    CHECK(e.message.find("unterminated") != std::string::npos);
  }
}

void test_lone_quote_after_async_function_is_passthrough() {
  // A GCC-tolerated lone quote outside every async island is not an error.
  auto parsed = parse_file("async int f() { return 1; }\n#if 0\nit's fine\n#endif\n");
  CHECK(std::holds_alternative<ParsedFile>(parsed));
  if (std::holds_alternative<ParsedFile>(parsed)) {
    CHECK_EQ(std::get<ParsedFile>(parsed).functions.size(), 1u);
  }
}

void test_async_in_directive_is_not_a_marker() {
  auto parsed = parse_file("#define ASYNC_KW async\nint plain() { return 1; }");
  CHECK(std::holds_alternative<ParsedFile>(parsed));
  if (std::holds_alternative<ParsedFile>(parsed)) {
    CHECK_EQ(std::get<ParsedFile>(parsed).functions.size(), 0u);
  }
}

void test_offsets_unchanged_by_port() {
  // The emitter slices on these offsets; pin their exact semantics.
  std::string_view src = "x; async int f(int a) /* c */ { return a; } y;";
  auto fn = expect_single(src);
  CHECK_EQ(fn.async_kw_offset, src.find("async"));
  CHECK_EQ(fn.body_open_offset, src.find('{') + 1);
  CHECK_EQ(fn.body_close_offset, src.find('}'));
  CHECK_EQ(fn.params, "(int a)");
}

void test_multiline_raw_string_in_define_inside_body() {
  // QA: the `}` inside the directive's raw string is not code; the body
  // must close at the real `}`.
  std::string_view src = "async int f() {\n#define J R\"(\n}\n)\"\n  return 1; }";
  auto fn = expect_single(src);
  CHECK_EQ(fn.body_close_offset, src.rfind('}'));
}

void test_brace_in_directive_inside_body_is_opaque() {
  // QA: a `}` in a #define inside the body is directive text, not code.
  std::string_view src = "async int f() {\n#define RB }\n  return 1;\n}\nint x;";
  auto fn = expect_single(src);
  CHECK_EQ(fn.body_close_offset, src.rfind('}'));
}

void test_lone_quote_in_if0_inside_body_is_error_at_quote() {
  // QA: pins the documented policy. GCC accepts this, but an Invalid token
  // inside an async island fails loudly at the quote (3:6), never silently.
  auto parsed = parse_file("async int f() {\n#if 0\n  don't\n#endif\n  return 1; }");
  CHECK(std::holds_alternative<ParseError>(parsed));
  if (std::holds_alternative<ParseError>(parsed)) {
    const auto& e = std::get<ParseError>(parsed);
    CHECK_EQ(e.line, 3u);
    CHECK_EQ(e.col, 6u);
  }
}

void test_multiline_raw_string_in_define_hides_async() {
  auto parsed = parse_file("#define D R\"(\nasync int g() { return 0; }\n)\"\nint x;");
  CHECK(std::holds_alternative<ParsedFile>(parsed));
  if (std::holds_alternative<ParsedFile>(parsed)) {
    CHECK_EQ(std::get<ParsedFile>(parsed).functions.size(), 0u);
  }
}

void test_unterminated_block_comment_is_error_at_opening() {
  auto parsed = parse_file("async int f() { return 1; }\n  /* never closes");
  CHECK(std::holds_alternative<ParseError>(parsed));
  if (std::holds_alternative<ParseError>(parsed)) {
    const auto& e = std::get<ParseError>(parsed);
    CHECK_EQ(e.line, 2u);
    CHECK_EQ(e.col, 3u);
  }
}

} // namespace

int main() {
  test_simple_signature();
  test_templated_return_type();
  test_reference_return_type();
  test_void_no_params();
  test_default_arg_with_nested_parens();
  test_nested_braces_in_body();
  test_multiple_functions_and_passthrough();
  test_string_and_comment_do_not_confuse_body_matching();
  test_known_limitation_function_pointer_return_type_errors();
  test_no_async_functions_is_empty_success();
  test_unterminated_body_is_error();
  test_digit_separator_before_async_function();
  test_apostrophe_in_if0_text_before_async_function();
  test_apostrophe_in_define_before_async_function();
  test_lone_quote_inside_async_body_is_error_at_quote();
  test_lone_quote_in_return_type_is_error_at_quote();
  test_lone_quote_in_params_is_error_at_quote();
  test_lone_quote_after_async_function_is_passthrough();
  test_async_in_directive_is_not_a_marker();
  test_offsets_unchanged_by_port();
  test_multiline_raw_string_in_define_inside_body();
  test_brace_in_directive_inside_body_is_opaque();
  test_lone_quote_in_if0_inside_body_is_error_at_quote();
  test_multiline_raw_string_in_define_hides_async();
  test_unterminated_block_comment_is_error_at_opening();
  return asynctp::test::report("parser_tests");
}
