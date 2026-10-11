#include "asynctp/async_parser.hpp"
#include "asynctp/lowering.hpp"
#include "test_support.hpp"

using namespace asynctp;

namespace {

std::string lower_single_body(std::string_view full_source) {
  auto parsed = parse_file(full_source);
  if (std::holds_alternative<ParseError>(parsed)) {
    CHECK(false);
    return {};
  }
  const auto& pf = std::get<ParsedFile>(parsed);
  if (pf.functions.size() != 1) {
    CHECK(false);
    return {};
  }
  auto lowered = lower_body(pf.functions[0].body);
  if (std::holds_alternative<ParseError>(lowered)) {
    const auto& e = std::get<ParseError>(lowered);
    std::cerr << "  unexpected lowering error: " << e.message << "\n";
    CHECK(false);
    return {};
  }
  return std::get<std::string>(lowered);
}

void test_simple_await() {
  auto out = lower_single_body("async int f() { int y = helper(x).await; return y; }");
  CHECK_EQ(out, " int y = co_await (helper(x)); co_return y; ");
}

void test_chained_call_await() {
  auto out = lower_single_body("async int f() { return a().b().await; }");
  CHECK_EQ(out, " co_return co_await (a().b()); ");
}

void test_await_call_not_rewritten() {
  // obj.await() is a real method call, not the .await pseudo-member.
  auto out = lower_single_body("async int f() { return obj.await(); }");
  CHECK_EQ(out, " co_return obj.await(); ");
}

void test_bare_return_and_return_value() {
  auto out = lower_single_body("async void f() { if (true) { return; } return; }");
  CHECK_EQ(out, " if (true) { co_return; } co_return; ");
}

void test_return_inside_ordinary_lambda_not_rewritten() {
  auto out = lower_single_body(
      "async int f() { auto g = [](int a) { return a + 1; }; return g(1); }");
  CHECK_EQ(out, " auto g = [](int a) { return a + 1; }; co_return g(1); ");
}

void test_return_lambda_itself_is_rewritten_capture_not_confused_with_subscript() {
  // The outer `return [x](){...}` is a lambda being returned (co_return
  // applies to the outer return); the inner `return x;` is inside that
  // ordinary lambda and must NOT be rewritten.
  auto out = lower_single_body("async int f(int x) { return [x]() { return x; }(); }");
  CHECK_EQ(out, " co_return [x]() { return x; }(); ");
}

void test_await_inside_ordinary_lambda_not_rewritten() {
  auto out = lower_single_body(
      "async int f() { auto g = []() { return helper().await; }; return 1; }");
  CHECK_EQ(out, " auto g = []() { return helper().await; }; co_return 1; ");
}

void test_subscript_not_confused_with_lambda() {
  auto out = lower_single_body("async int f(int* arr) { return arr[0]; }");
  CHECK_EQ(out, " co_return arr[0]; ");
}

void test_await_with_parenthesized_complex_expression() {
  auto out = lower_single_body("async int f(bool c) { return (c ? a() : b()).await; }");
  CHECK_EQ(out, " co_return co_await ((c ? a() : b())); ");
}

void test_string_and_comment_not_confused_for_return_or_await() {
  auto out = lower_single_body(
      "async int f() { const char* s = \"return .await\"; // return .await\n return 1; }");
  CHECK_EQ(out, " const char* s = \"return .await\"; // return .await\n co_return 1; ");
}

void test_nested_ordinary_block_is_still_rewritten() {
  auto out = lower_single_body("async int f() { for (int i = 0; i < 1; i++) { return helper().await; } }");
  CHECK_EQ(out, " for (int i = 0; i < 1; i++) { co_return co_await (helper()); } ");
}

void test_await_after_line_comment_ending_in_period() {
  // The '.' ending the comment must not be read as member access chaining
  // the comment's words into the awaited expression.
  auto out = lower_single_body("async void f() {\n  // let others run.\n  ns::yield_now().await;\n}");
  CHECK_EQ(out, "\n  // let others run.\n  co_await (ns::yield_now());\n");
}

void test_await_after_block_comment() {
  auto out = lower_single_body("async int f() { return /* a.b */ helper().await; }");
  CHECK_EQ(out, " co_return /* a.b */ co_await (helper()); ");
}

void test_comment_inside_awaited_call_args_is_kept_in_expression() {
  auto out = lower_single_body("async int f() { return helper(/* x */ 1).await; }");
  CHECK_EQ(out, " co_return co_await (helper(/* x */ 1)); ");
}

} // namespace

int main() {
  test_simple_await();
  test_chained_call_await();
  test_await_call_not_rewritten();
  test_bare_return_and_return_value();
  test_return_inside_ordinary_lambda_not_rewritten();
  test_return_lambda_itself_is_rewritten_capture_not_confused_with_subscript();
  test_await_inside_ordinary_lambda_not_rewritten();
  test_subscript_not_confused_with_lambda();
  test_await_with_parenthesized_complex_expression();
  test_string_and_comment_not_confused_for_return_or_await();
  test_nested_ordinary_block_is_still_rewritten();
  test_await_after_line_comment_ending_in_period();
  test_await_after_block_comment();
  test_comment_inside_awaited_call_args_is_kept_in_expression();
  return asynctp::test::report("lowering_tests");
}
