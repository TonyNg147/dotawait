#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace asynctp::lsp {

// Byte offset into the text given to Json::parse, plus a human-readable
// reason. The offset points at the first byte that made the input invalid
// (for an unterminated string or a bad escape: where that string/escape
// starts, since that is where a reader needs to look).
struct JsonError {
  std::size_t offset;
  std::string message;
};

// Minimal JSON DOM for the LSP proxy (asynctp-lsp). Standard library only:
// the zero-dependency rule for transpiler/ is why this exists instead of
// nlohmann/json.
//
// Parsing is RFC 8259 strict: no comments, no trailing commas, no leading
// `+`/zeros, strings must be valid UTF-8 with no raw control characters, and
// `\uXXXX` surrogate pairs are combined into one UTF-8 code point (a lone
// surrogate is an error). Nesting deeper than kMaxDepth arrays/objects is an
// error, so hostile input can't exhaust the stack.
//
// Numbers: a literal with no `.`/exponent that fits int64 is an int (exact,
// which is what LSP ids/positions need); anything else is a double. `-0`
// parses as the int 0. A magnitude outside double's range (overflow like
// 1e400 or underflow like 1e-400) is an error, as RFC 8259 section 6
// permits. Duplicate object keys are kept in input order.
//
// No exceptions (transpiler convention), so the accessor contract is:
//   - is_*() tell you the kind.
//   - as_bool/as_int/as_double/as_string return the given default on the
//     wrong kind. as_double also accepts an int (widened); as_int does NOT
//     accept a double (no silent truncation).
//   - as_array/as_object/find return nullptr on the wrong kind; find also
//     returns nullptr for a missing key and the FIRST match for a
//     duplicated one.
//   - operator[](key) and push_back mutate in place: on any value that is
//     not already an object (resp. array), including null, the value is
//     first REPLACED by an empty one. operator[] appends a null member for a
//     missing key, so building an object keeps insertion order.
class Json {
public:
  using Array = std::vector<Json>;
  // Insertion order is preserved (a vector, not a map) so forwarded messages
  // stay recognisable in logs and parse+dump round-trips byte-for-byte.
  using Object = std::vector<std::pair<std::string, Json>>;

  static constexpr int kMaxDepth = 256;

  Json() noexcept = default; // null
  Json(std::nullptr_t) noexcept {}
  Json(bool b) noexcept : v_(b) {}
  // Any integer type except bool, stored as int64 (a uint64 above INT64_MAX
  // wraps; LSP never needs one).
  template <typename I>
    requires(std::is_integral_v<I> && !std::is_same_v<I, bool>)
  Json(I i) noexcept : v_(static_cast<std::int64_t>(i)) {}
  Json(double d) noexcept : v_(d) {}
  Json(std::string s) : v_(std::move(s)) {}
  Json(std::string_view s) : v_(std::string(s)) {}
  Json(const char* s) : v_(std::string(s)) {} // else a literal would pick bool
  Json(Array a) : v_(std::move(a)) {}
  Json(Object o) : v_(std::move(o)) {}

  static std::variant<Json, JsonError> parse(std::string_view text);

  // Compact (no whitespace). Escapes `"`, `\` and bytes < 0x20 (as
  // `\u00XX`); every other byte, including non-ASCII UTF-8, is passed
  // through. Doubles use the shortest round-trip form from std::to_chars,
  // with ".0" appended if that form looks like an integer so the value
  // re-parses as a double. NaN/infinity have no JSON form and dump as null.
  std::string dump() const;

  bool is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(v_); }
  bool is_bool() const noexcept { return std::holds_alternative<bool>(v_); }
  bool is_int() const noexcept { return std::holds_alternative<std::int64_t>(v_); }
  bool is_double() const noexcept { return std::holds_alternative<double>(v_); }
  bool is_number() const noexcept { return is_int() || is_double(); }
  bool is_string() const noexcept { return std::holds_alternative<std::string>(v_); }
  bool is_array() const noexcept { return std::holds_alternative<Array>(v_); }
  bool is_object() const noexcept { return std::holds_alternative<Object>(v_); }

  bool as_bool(bool def = false) const noexcept;
  std::int64_t as_int(std::int64_t def = 0) const noexcept;
  double as_double(double def = 0.0) const noexcept;
  // The view is into this Json; it dangles once the value is modified.
  std::string_view as_string(std::string_view def = {}) const noexcept;
  const Array* as_array() const noexcept { return std::get_if<Array>(&v_); }
  Array* as_array() noexcept { return std::get_if<Array>(&v_); }
  const Object* as_object() const noexcept { return std::get_if<Object>(&v_); }
  Object* as_object() noexcept { return std::get_if<Object>(&v_); }

  const Json* find(std::string_view key) const noexcept;
  Json* find(std::string_view key) noexcept;

  // References/pointers into a container follow std::vector rules: adding a
  // member/element may invalidate earlier ones.
  Json& operator[](std::string_view key);
  void push_back(Json value);

private:
  void dump_to(std::string& out) const;

  std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object> v_;
};

} // namespace asynctp::lsp
