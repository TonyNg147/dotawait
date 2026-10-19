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

// `return` is rewritten wherever it appears, not only as the first token
// after `;`/`{`/`}`: braceless if/else/loop bodies and case/default/labels.
void test_braceless_if_return_mixed_with_plain_return() {
  auto out = lower_single_body("async int f(bool c) { if (c) return 1; return 2; }");
  CHECK_EQ(out, " if (c) co_return 1; co_return 2; ");
}

void test_braceless_if_else_return() {
  auto out = lower_single_body("async int f(bool c) { if (c) return 1; else return 2; }");
  CHECK_EQ(out, " if (c) co_return 1; else co_return 2; ");
}

void test_braceless_while_return() {
  auto out = lower_single_body("async void f(int n) { while (n) return; }");
  CHECK_EQ(out, " while (n) co_return; ");
}

void test_braceless_for_return() {
  auto out = lower_single_body("async int f() { for (;;) return 1; }");
  CHECK_EQ(out, " for (;;) co_return 1; ");
}

void test_braceless_do_while_return() {
  auto out = lower_single_body("async void f(int n) { do return; while (n); }");
  CHECK_EQ(out, " do co_return; while (n); ");
}

void test_case_and_default_return() {
  auto out = lower_single_body("async int f(int k) { switch (k) { case 1: return 1; default: return 0; } }");
  CHECK_EQ(out, " switch (k) { case 1: co_return 1; default: co_return 0; } ");
}

void test_label_return() {
  auto out = lower_single_body("async int f() { again: return 1; }");
  CHECK_EQ(out, " again: co_return 1; ");
}

void test_braceless_return_with_await() {
  auto out = lower_single_body("async int f(bool c) { if (c) return g().await; return 0; }");
  CHECK_EQ(out, " if (c) co_return co_await (g()); co_return 0; ");
}

void test_braceless_return_inside_ordinary_lambda_not_rewritten() {
  auto out = lower_single_body(
      "async int f(bool c) { auto l = [](bool c) { if (c) return 1; return 2; }; if (c) return l(c); else return 0; }");
  CHECK_EQ(out, " auto l = [](bool c) { if (c) return 1; return 2; }; if (c) co_return l(c); else co_return 0; ");
}

// QA: no whitespace around the keyword; `return` glued to `(`, `-`, `+`, `:`.
void test_braceless_return_without_whitespace() {
  auto out = lower_single_body(
      "async int f(int k) { if(k)return(1);switch(k){case 1:return-1;default:return+2;} }");
  CHECK_EQ(out, " if(k)co_return(1);switch(k){case 1:co_return-1;default:co_return+2;} ");
}

// QA: braceless returns inside lambdas passed as call arguments (after `(`
// and `,`) stay `return`; `co_return` and look-alike identifiers are left as is.
void test_braceless_return_in_call_arg_lambdas_and_lookalikes() {
  auto out = lower_single_body(
      "async int f(bool c) { sort(b, e, [](int a, int z) { if (a < z) return true; return false; }); "
      "int r = call([](int x) { if (x) return x; return 0; }); "
      "if (c) co_return returned; else return my_return + r; }");
  CHECK_EQ(out,
           " sort(b, e, [](int a, int z) { if (a < z) return true; return false; }); "
           "int r = call([](int x) { if (x) return x; return 0; }); "
           "if (c) co_return returned; else co_return my_return + r; ");
}

// QA: a braceless `return` whose operand is a lambda: the outer keyword is
// rewritten, the lambda body's braceless returns are not.
void test_braceless_return_of_lambda_with_braceless_returns() {
  auto out = lower_single_body(
      "async int f(bool c) { if (c) return [](int x) { if (x) return 1; return 2; }(0); return 0; }");
  CHECK_EQ(out, " if (c) co_return [](int x) { if (x) return 1; return 2; }(0); co_return 0; ");
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
  test_braceless_if_return_mixed_with_plain_return();
  test_braceless_if_else_return();
  test_braceless_while_return();
  test_braceless_for_return();
  test_braceless_do_while_return();
  test_case_and_default_return();
  test_label_return();
  test_braceless_return_with_await();
  test_braceless_return_inside_ordinary_lambda_not_rewritten();
  test_braceless_return_without_whitespace();
  test_braceless_return_in_call_arg_lambdas_and_lookalikes();
  test_braceless_return_of_lambda_with_braceless_returns();
  return asynctp::test::report("lowering_tests");
}
