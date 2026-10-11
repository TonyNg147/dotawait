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
  return asynctp::test::report("parser_tests");
}
