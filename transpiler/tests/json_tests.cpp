#include "asynctp/lsp/json.hpp"
#include "test_support.hpp"

#include <limits>
#include <string>
#include <string_view>
#include <variant>

using namespace asynctp::lsp;

namespace {

bool parses(std::string_view text) {
  return std::holds_alternative<Json>(Json::parse(text));
}

Json parse_ok(std::string_view text) {
  auto result = Json::parse(text);
  CHECK(std::holds_alternative<Json>(result));
  if (auto* err = std::get_if<JsonError>(&result)) {
    std::cerr << "  unexpected JsonError at " << err->offset << ": " << err->message << "\n";
    return Json{};
  }
  return std::get<Json>(std::move(result));
}

// Returns the error offset, or SIZE_MAX if the text unexpectedly parsed.
std::size_t error_offset(std::string_view text) {
  auto result = Json::parse(text);
  if (auto* err = std::get_if<JsonError>(&result)) {
    CHECK(!err->message.empty());
    return err->offset;
  }
  return static_cast<std::size_t>(-1);
}

void test_round_trip_initialize_request() {
  std::string_view text =
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":null,"capabilities":{}}})";
  Json j = parse_ok(text);
  CHECK_EQ(j.dump(), std::string(text));
  CHECK(j.is_object());
  CHECK(j.find("id") != nullptr && j.find("id")->is_int());
  CHECK_EQ(j.find("method")->as_string(), "initialize");
  const Json* params = j.find("params");
  CHECK(params != nullptr && params->find("rootUri") != nullptr);
  CHECK(params->find("rootUri")->is_null());
  CHECK(params->find("capabilities")->is_object());
}

void test_whitespace_is_dropped_on_dump() {
  Json j = parse_ok(" \t\r\n{ \"a\" : [ 1 , true , false , null ] , \"b\" : \"x\" }\n");
  CHECK_EQ(j.dump(), R"({"a":[1,true,false,null],"b":"x"})");
}

void test_integers_are_exact_int64() {
  Json zero = parse_ok("0");
  CHECK(zero.is_int());
  CHECK_EQ(zero.as_int(), 0);
  CHECK_EQ(zero.dump(), "0");

  Json minus_one = parse_ok("-1");
  CHECK(minus_one.is_int());
  CHECK_EQ(minus_one.as_int(), -1);
  CHECK_EQ(minus_one.dump(), "-1");

  // 2^53 + 1: not representable as a double, so it must stay an int64.
  Json big = parse_ok("9007199254740993");
  CHECK(big.is_int());
  CHECK_EQ(big.as_int(), 9007199254740993LL);
  CHECK_EQ(big.dump(), "9007199254740993");
}

void test_int64_bounds_and_overflow_to_double() {
  Json max = parse_ok("9223372036854775807");
  CHECK(max.is_int());
  CHECK_EQ(max.dump(), "9223372036854775807");
  Json min = parse_ok("-9223372036854775808");
  CHECK(min.is_int());
  CHECK_EQ(min.dump(), "-9223372036854775808");

  Json over = parse_ok("9223372036854775808");
  CHECK(over.is_double());
  CHECK(!over.is_int());
  CHECK(over.is_number());
}

void test_doubles() {
  Json a = parse_ok("1.5");
  CHECK(a.is_double());
  CHECK_EQ(a.as_double(), 1.5);
  CHECK_EQ(a.dump(), "1.5");

  Json b = parse_ok("1e-3");
  CHECK(b.is_double());
  CHECK_EQ(b.as_double(), 0.001);
  CHECK_EQ(b.dump(), "0.001");

  Json c = parse_ok("-2.5E+2");
  CHECK(c.is_double());
  CHECK_EQ(c.as_double(), -250.0);
}

void test_number_edge_cases() {
  Json neg_zero = parse_ok("-0");
  CHECK(neg_zero.is_int());
  CHECK_EQ(neg_zero.dump(), "0");
  CHECK(parse_ok("-0.0").is_double());
  CHECK(parse_ok("1E2").is_double());
  CHECK_EQ(parse_ok("1E2").as_double(), 100.0);
  CHECK(parse_ok("123456789012345678901234567890").is_double());
  CHECK(parse_ok("4.9e-324").is_double()); // smallest subnormal is in range
  CHECK_EQ(parse_ok("0.1").dump(), "0.1");
  // Magnitudes outside double's range are errors (RFC 8259 section 6).
  CHECK_EQ(error_offset("[1e400]"), 1u);
  CHECK_EQ(error_offset("-1e-400"), 0u);
}

void test_integral_double_keeps_its_type_through_dump() {
  // to_chars prints 2.0 as "2"; re-parsing that would flip the type to int.
  Json j = parse_ok("2.0");
  CHECK(j.is_double());
  CHECK_EQ(j.dump(), "2.0");
  CHECK(parse_ok(j.dump()).is_double());
  CHECK_EQ(Json(1e300).dump(), "1e+300");
}

void test_non_finite_double_dumps_as_null() {
  CHECK_EQ(Json(std::numeric_limits<double>::infinity()).dump(), "null");
  CHECK_EQ(Json(std::numeric_limits<double>::quiet_NaN()).dump(), "null");
}

void test_simple_escapes() {
  Json j = parse_ok(R"("\"\\\/\b\f\n\r\t")");
  CHECK_EQ(j.as_string(), std::string_view("\"\\/\b\f\n\r\t"));
}

void test_unicode_escape_to_utf8() {
  CHECK_EQ(parse_ok(R"("é")").as_string(), "\xC3\xA9");      // e-acute, 2 bytes
  CHECK_EQ(parse_ok(R"("€")").as_string(), "\xE2\x82\xAC");  // euro sign, 3 bytes
  CHECK_EQ(parse_ok(R"("A")").as_string(), "A");
  // An escaped NUL is a legal string byte and survives a round trip.
  Json nul = parse_ok(R"("a\u0000b")");
  CHECK_EQ(nul.as_string(), std::string_view("a\0b", 3));
  CHECK_EQ(nul.dump(), R"("a\u0000b")");
}

void test_surrogate_pair_to_utf8() {
  CHECK_EQ(parse_ok(R"("😀")").as_string(), "\xF0\x9F\x98\x80"); // U+1F600
}

void test_lone_surrogates_are_errors() {
  CHECK_EQ(error_offset(R"("\ud83d")"), 1u);
  CHECK_EQ(error_offset(R"("\ud83dx")"), 1u);
  CHECK_EQ(error_offset(R"("\ud83dA")"), 1u); // high + non-low
  CHECK_EQ(error_offset(R"("\ude00")"), 1u);       // low without high
}

// QA (T-0019): the two tests above spell their inputs as raw UTF-8, so they
// never reach the \uXXXX decoder. These go through it: every UTF-8 length
// boundary of append_utf8, plus escaped surrogate pairs.
void test_backslash_u_escapes_encode_utf8() {
  CHECK_EQ(parse_ok(R"("é")").as_string(), "\xC3\xA9");
  CHECK_EQ(parse_ok(R"("é")").as_string(), "\xC3\xA9");
  CHECK_EQ(parse_ok(R"("€")").as_string(), "\xE2\x82\xAC");
  CHECK_EQ(parse_ok(R"("A")").as_string(), "A");
  CHECK_EQ(parse_ok(R"("\u007f")").as_string(), "\x7F");
  CHECK_EQ(parse_ok(R"("\u0080")").as_string(), "\xC2\x80");
  CHECK_EQ(parse_ok(R"("߿")").as_string(), "\xDF\xBF");
  CHECK_EQ(parse_ok(R"("ࠀ")").as_string(), "\xE0\xA0\x80");
  CHECK_EQ(parse_ok(R"("￿")").as_string(), "\xEF\xBF\xBF");
  // Decoded text dumps as raw UTF-8, not re-escaped.
  CHECK_EQ(parse_ok(R"("café")").dump(), "\"caf\xC3\xA9\"");
}

void test_backslash_u_surrogate_pairs_combine() {
  CHECK_EQ(parse_ok(R"("😀")").as_string(), "\xF0\x9F\x98\x80"); // U+1F600
  CHECK_EQ(parse_ok(R"("😀")").as_string(), "\xF0\x9F\x98\x80");
  CHECK_EQ(parse_ok(R"("𐀀")").as_string(), "\xF0\x90\x80\x80"); // U+10000
  CHECK_EQ(parse_ok(R"("􏿿")").as_string(), "\xF4\x8F\xBF\xBF"); // U+10FFFF
  CHECK_EQ(parse_ok(R"("a😀b")").dump(), "\"a\xF0\x9F\x98\x80" "b\"");
}

void test_backslash_u_lone_surrogate_variants() {
  CHECK_EQ(error_offset(R"("\ud83d\ud83d")"), 1u); // high + high
  CHECK_EQ(error_offset(R"("\ud83dA")"), 1u); // high + escaped non-surrogate
  CHECK_EQ(error_offset(R"("\ud83d\n")"), 1u);     // high + simple escape
  CHECK_EQ(error_offset(R"("\ud83d\u")"), 1u);     // high + truncated escape
  CHECK_EQ(error_offset(R"(["x","\ude00\ud83d"])"), 6u); // reversed pair
  CHECK_EQ(error_offset(R"("\u12")"), 1u);
  CHECK_EQ(error_offset(R"("\U00e9")"), 1u);
  CHECK_EQ(error_offset("\"\\"), 1u);
}

void test_more_number_grammar() {
  CHECK(parse_ok("1e-320").is_double()); // subnormal, in range
  CHECK(parse_ok("0e0").is_double());
  CHECK(parse_ok("-9223372036854775809").is_double());
  CHECK(parse_ok("123.0").is_double());
  CHECK_EQ(error_offset("-01"), 2u);
  CHECK_EQ(error_offset("1.e5"), 2u);
  CHECK_EQ(error_offset("1.5e+"), 5u);
  CHECK_EQ(error_offset("[-]"), 2u);
  CHECK_EQ(error_offset("- 1"), 1u);
  CHECK_EQ(error_offset("00"), 1u);
}

void test_more_double_dumps_round_trip() {
  CHECK_EQ(Json(-0.0).dump(), "-0.0");
  CHECK(parse_ok(Json(-0.0).dump()).is_double());
  CHECK_EQ(Json(1e21).dump(), "1e+21");
  CHECK_EQ(Json(5e-324).dump(), "5e-324");
  CHECK_EQ(Json(0.1 + 0.2).dump(), "0.30000000000000004");
  CHECK_EQ(parse_ok(Json(0.1 + 0.2).dump()).as_double(), 0.1 + 0.2);
}

void test_more_invalid_input() {
  CHECK_EQ(error_offset("\xEF\xBB\xBF{}"), 0u); // BOM is not JSON whitespace
  CHECK_EQ(error_offset("\v1"), 0u);
  CHECK_EQ(error_offset("[,1]"), 1u);
  CHECK_EQ(error_offset("{,}"), 1u);
  CHECK_EQ(error_offset("{\"a\"}"), 4u);
  CHECK_EQ(error_offset("[1]]"), 3u);
  CHECK_EQ(error_offset("nulll"), 4u);
  CHECK_EQ(error_offset("\"\xF4\x90\x80\x80\""), 1u); // above U+10FFFF
  CHECK_EQ(error_offset("\"\xF0\x8F\xBF\xBF\""), 1u); // overlong 4-byte
  CHECK_EQ(error_offset("\"\xE0\x9F\xBF\""), 1u);     // overlong 3-byte
  CHECK_EQ(error_offset("\"\x80\""), 1u);             // stray continuation
  CHECK(parses("{\"\":\"\"}"));
  CHECK(parses("{\"a\":1}\n\n"));
}

void test_raw_utf8_passes_through() {
  Json j = parse_ok("\"\xC3\xA9\xF0\x9F\x98\x80\"");
  CHECK_EQ(j.as_string(), "\xC3\xA9\xF0\x9F\x98\x80");
  CHECK_EQ(j.dump(), "\"\xC3\xA9\xF0\x9F\x98\x80\"");
}

void test_invalid_utf8_is_an_error() {
  CHECK_EQ(error_offset("\"a\xFF\""), 2u);         // never valid in UTF-8
  CHECK_EQ(error_offset("\"\xC3\""), 1u);          // truncated sequence
  CHECK_EQ(error_offset("\"\xC0\xAF\""), 1u);      // overlong '/'
  CHECK_EQ(error_offset("\"\xED\xA0\x80\""), 1u);  // UTF-8-encoded surrogate
}

void test_dump_escapes_control_characters() {
  Json j(std::string("a\x01\n\x1f\"\\/\x7f"));
  CHECK_EQ(j.dump(), "\"a\\u0001\\u000a\\u001f\\\"\\\\/\x7f\"");
  CHECK_EQ(parse_ok(j.dump()).as_string(), std::string("a\x01\n\x1f\"\\/\x7f"));
}

void test_dump_passes_non_ascii_through() {
  CHECK_EQ(Json("\xC3\xA9").dump(), "\"\xC3\xA9\"");
}

void test_key_order_preserved() {
  std::string_view text = R"({"z":1,"a":2,"m":{"y":true,"b":false}})";
  CHECK_EQ(parse_ok(text).dump(), std::string(text));

  Json built;
  built["z"] = 1;
  built["a"] = "two";
  built["m"]["y"] = true;
  CHECK_EQ(built.dump(), R"({"z":1,"a":"two","m":{"y":true}})");
  built["z"] = 3; // existing key is updated in place, not re-appended
  CHECK_EQ(built.dump(), R"({"z":3,"a":"two","m":{"y":true}})");
}

void test_error_offsets() {
  CHECK_EQ(error_offset(R"({"a":})"), 5u);
  CHECK_EQ(error_offset("[1,]"), 3u);
  CHECK_EQ(error_offset("tru"), 3u);
  CHECK_EQ(error_offset(R"(["abc)"), 1u); // points at the opening quote
  CHECK_EQ(error_offset(R"("abc)"), 0u);
}

void test_other_syntax_errors() {
  CHECK_EQ(error_offset(""), 0u);
  CHECK_EQ(error_offset("   "), 3u);
  CHECK_EQ(error_offset("1 2"), 2u);   // trailing content
  CHECK_EQ(error_offset("true false"), 5u);
  CHECK_EQ(error_offset("{} x"), 3u);
  CHECK_EQ(error_offset(std::string_view("[1]\0", 4)), 3u); // embedded NUL is not whitespace
  CHECK_EQ(error_offset(std::string_view("[\0]", 3)), 1u);
  CHECK_EQ(error_offset("01"), 1u);    // leading zero
  CHECK_EQ(error_offset("1."), 2u);
  CHECK_EQ(error_offset("1e"), 2u);
  CHECK_EQ(error_offset("-"), 1u);
  CHECK_EQ(error_offset("+1"), 0u);
  CHECK_EQ(error_offset(".5"), 0u);
  CHECK_EQ(error_offset("1e400"), 0u); // out of double range
  CHECK_EQ(error_offset("nul"), 3u);
  CHECK_EQ(error_offset("trux"), 3u);
  CHECK_EQ(error_offset("{\"a\" 1}"), 5u);
  CHECK_EQ(error_offset("{1:2}"), 1u);
  CHECK_EQ(error_offset("{\"a\":1,}"), 7u);
  CHECK_EQ(error_offset("[1 2]"), 3u);
  CHECK_EQ(error_offset("\"a\nb\""), 2u);   // raw control character
  CHECK_EQ(error_offset(R"("\x")"), 1u);    // unknown escape
  CHECK_EQ(error_offset(R"("\u12G4")"), 1u);
  CHECK_EQ(error_offset("// c\n1"), 0u);    // comments are not JSON
}

std::string nested_arrays(int depth) {
  return std::string(static_cast<std::size_t>(depth), '[') +
         std::string(static_cast<std::size_t>(depth), ']');
}

void test_max_depth() {
  CHECK(parses(nested_arrays(256)));
  // The 257th '[' (offset 256) is the one that exceeds the limit.
  CHECK_EQ(error_offset(nested_arrays(257)), 256u);
  // Deep enough to blow the stack without the limit.
  CHECK(!parses(nested_arrays(100000)));

  std::string objects;
  for (int i = 0; i < 257; ++i) objects += "{\"a\":";
  objects += "1";
  objects += std::string(257, '}');
  CHECK(!parses(objects));
}

void test_wrong_type_accessors_return_defaults() {
  Json n(42);
  CHECK(n.is_int());
  CHECK_EQ(n.as_string(), "");
  CHECK_EQ(n.as_string("fallback"), "fallback");
  CHECK(n.as_array() == nullptr);
  CHECK(n.as_object() == nullptr);
  CHECK(n.find("x") == nullptr);
  CHECK_EQ(n.as_bool(), false);
  CHECK_EQ(n.as_bool(true), true);
  CHECK_EQ(n.as_double(), 42.0); // ints widen to double

  Json s("text");
  CHECK_EQ(s.as_int(), 0);
  CHECK_EQ(s.as_int(-7), -7);
  CHECK_EQ(s.as_double(), 0.0);

  Json d(1.5);
  CHECK_EQ(d.as_int(-1), -1); // a double is not silently truncated to an int

  const Json obj = parse_ok(R"({"a":1})");
  CHECK(obj.find("missing") == nullptr);
  CHECK(obj.find("a") != nullptr);
}

void test_duplicate_keys_preserved_and_find_returns_first() {
  Json j = parse_ok(R"({"k":1,"k":2})");
  CHECK_EQ(j.dump(), R"({"k":1,"k":2})");
  CHECK_EQ(j.find("k")->as_int(), 1);
}

void test_mutation() {
  Json arr;
  arr.push_back(1);
  arr.push_back("two");
  arr.push_back(Json::Array{});
  arr.push_back(nullptr);
  CHECK_EQ(arr.dump(), R"([1,"two",[],null])");
  CHECK(arr.as_array() != nullptr && arr.as_array()->size() == 4);

  Json obj = parse_ok(R"({"a":{"b":1}})");
  if (Json* b = obj.find("a") ? obj.find("a")->find("b") : nullptr) *b = 2;
  CHECK_EQ(obj.dump(), R"({"a":{"b":2}})");

  // operator[] / push_back on the wrong kind replace the value (documented).
  Json scalar(5);
  scalar["k"] = false;
  CHECK_EQ(scalar.dump(), R"({"k":false})");
  Json str("s");
  str.push_back(1);
  CHECK_EQ(str.dump(), "[1]");

  CHECK_EQ(Json().dump(), "null");
  CHECK_EQ(Json(Json::Object{}).dump(), "{}");
}

} // namespace

int main() {
  test_round_trip_initialize_request();
  test_whitespace_is_dropped_on_dump();
  test_integers_are_exact_int64();
  test_int64_bounds_and_overflow_to_double();
  test_doubles();
  test_number_edge_cases();
  test_integral_double_keeps_its_type_through_dump();
  test_non_finite_double_dumps_as_null();
  test_simple_escapes();
  test_unicode_escape_to_utf8();
  test_surrogate_pair_to_utf8();
  test_lone_surrogates_are_errors();
  test_backslash_u_escapes_encode_utf8();
  test_backslash_u_surrogate_pairs_combine();
  test_backslash_u_lone_surrogate_variants();
  test_more_number_grammar();
  test_more_double_dumps_round_trip();
  test_more_invalid_input();
  test_raw_utf8_passes_through();
  test_invalid_utf8_is_an_error();
  test_dump_escapes_control_characters();
  test_dump_passes_non_ascii_through();
  test_key_order_preserved();
  test_error_offsets();
  test_other_syntax_errors();
  test_max_depth();
  test_wrong_type_accessors_return_defaults();
  test_duplicate_keys_preserved_and_find_returns_first();
  test_mutation();
  return asynctp::test::report("json_tests");
}
