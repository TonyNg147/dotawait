#include "asynctp/lsp/json.hpp"

#include <charconv>
#include <cmath>
#include <optional>
#include <system_error>

namespace asynctp::lsp {

namespace {

// Recursive-descent parser. Sub-parsers return false after recording the
// first error, which keeps the error offset pointing at the innermost cause.
class Parser {
public:
  explicit Parser(std::string_view text) : text_(text) {}

  std::variant<Json, JsonError> run() {
    Json value;
    if (!parse_value(value, 0)) return std::move(*error_);
    skip_whitespace();
    if (!at_end()) {
      fail(pos_, "unexpected content after the JSON value");
      return std::move(*error_);
    }
    return value;
  }

private:
  bool at_end() const noexcept { return pos_ >= text_.size(); }

  // 0 at end of input. No caller accepts a NUL byte, so `peek() == c` needs
  // no separate at_end() check.
  unsigned char peek() const noexcept {
    return at_end() ? 0 : static_cast<unsigned char>(text_[pos_]);
  }

  bool fail(std::size_t offset, std::string message) {
    if (!error_) error_ = JsonError{offset, std::move(message)};
    return false;
  }

  // RFC 8259 whitespace only: no comments, no other Unicode spaces.
  void skip_whitespace() noexcept {
    while (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r') ++pos_;
  }

  // `depth` is the number of arrays/objects enclosing this value.
  bool parse_value(Json& out, int depth) {
    skip_whitespace();
    if (at_end()) return fail(pos_, "expected a value but reached end of input");
    switch (peek()) {
    case '{': return parse_object(out, depth);
    case '[': return parse_array(out, depth);
    case '"': {
      std::string s;
      if (!parse_string(s)) return false;
      out = Json(std::move(s));
      return true;
    }
    case 't': return parse_literal("true", Json(true), out);
    case 'f': return parse_literal("false", Json(false), out);
    case 'n': return parse_literal("null", Json(nullptr), out);
    default:
      if (peek() == '-' || (peek() >= '0' && peek() <= '9')) return parse_number(out);
      return fail(pos_, "unexpected character, expected a value");
    }
  }

  // The depth check happens before recursing, so nesting is bounded by
  // kMaxDepth stack frames no matter how deep the input is.
  bool enter_container(int depth) {
    if (depth >= Json::kMaxDepth) {
      return fail(pos_, "nesting deeper than " + std::to_string(Json::kMaxDepth) +
                            " arrays/objects");
    }
    ++pos_; // the '[' or '{'
    return true;
  }

  bool parse_array(Json& out, int depth) {
    if (!enter_container(depth)) return false;
    Json::Array items;
    skip_whitespace();
    if (peek() == ']') {
      ++pos_;
      out = Json(std::move(items));
      return true;
    }
    for (;;) {
      Json item;
      if (!parse_value(item, depth + 1)) return false;
      items.push_back(std::move(item));
      skip_whitespace();
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      if (peek() == ']') {
        ++pos_;
        out = Json(std::move(items));
        return true;
      }
      return fail(pos_, "expected ',' or ']' in array");
    }
  }

  bool parse_object(Json& out, int depth) {
    if (!enter_container(depth)) return false;
    Json::Object members;
    skip_whitespace();
    if (peek() == '}') {
      ++pos_;
      out = Json(std::move(members));
      return true;
    }
    for (;;) {
      skip_whitespace();
      if (peek() != '"') return fail(pos_, "expected a string object key");
      std::string key;
      if (!parse_string(key)) return false;
      skip_whitespace();
      if (peek() != ':') return fail(pos_, "expected ':' after object key");
      ++pos_;
      Json value;
      if (!parse_value(value, depth + 1)) return false;
      members.emplace_back(std::move(key), std::move(value));
      skip_whitespace();
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      if (peek() == '}') {
        ++pos_;
        out = Json(std::move(members));
        return true;
      }
      return fail(pos_, "expected ',' or '}' in object");
    }
  }

  bool parse_literal(std::string_view word, Json value, Json& out) {
    for (std::size_t i = 0; i < word.size(); ++i) {
      if (pos_ + i >= text_.size() || text_[pos_ + i] != word[i]) {
        return fail(pos_ + i, "invalid literal, expected '" + std::string(word) + "'");
      }
    }
    pos_ += word.size();
    out = std::move(value);
    return true;
  }

  bool skip_digits() noexcept {
    std::size_t start = pos_;
    while (peek() >= '0' && peek() <= '9') ++pos_;
    return pos_ != start;
  }

  bool parse_number(Json& out) {
    std::size_t start = pos_;
    if (peek() == '-') ++pos_;
    if (peek() == '0') {
      ++pos_;
      if (peek() >= '0' && peek() <= '9') return fail(pos_, "leading zeros are not allowed");
    } else if (!skip_digits()) {
      return fail(pos_, "expected a digit");
    }
    bool integral = true;
    if (peek() == '.') {
      integral = false;
      ++pos_;
      if (!skip_digits()) return fail(pos_, "expected a digit after '.'");
    }
    if (peek() == 'e' || peek() == 'E') {
      integral = false;
      ++pos_;
      if (peek() == '+' || peek() == '-') ++pos_;
      if (!skip_digits()) return fail(pos_, "expected a digit in the exponent");
    }

    // The grammar is validated above, so from_chars only ever sees a valid
    // JSON number (on its own it would also accept "inf", "nan", ...).
    const char* first = text_.data() + start;
    const char* last = text_.data() + pos_;
    if (integral) {
      std::int64_t i = 0;
      auto [ptr, ec] = std::from_chars(first, last, i);
      if (ec == std::errc{} && ptr == last) {
        out = Json(i);
        return true;
      }
      // Out of int64 range: fall through to double.
    }
    double d = 0.0;
    auto [ptr, ec] = std::from_chars(first, last, d);
    if (ec != std::errc{} || ptr != last) {
      // RFC 8259 section 6 lets implementations limit the accepted range.
      return fail(start, "number is outside the range of a double");
    }
    out = Json(d);
    return true;
  }

  static int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  // Reads `\uXXXX` starting at `at` (the backslash).
  std::optional<unsigned> read_u_escape(std::size_t at) const noexcept {
    if (at + 6 > text_.size() || text_[at] != '\\' || text_[at + 1] != 'u') return std::nullopt;
    unsigned cp = 0;
    for (std::size_t i = at + 2; i < at + 6; ++i) {
      int h = hex_value(text_[i]);
      if (h < 0) return std::nullopt;
      cp = cp * 16 + static_cast<unsigned>(h);
    }
    return cp;
  }

  static void append_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  // Length of the well-formed UTF-8 sequence starting at pos_, or 0 if it is
  // malformed or truncated. Follows the RFC 3629 byte table, so overlongs,
  // UTF-8-encoded surrogates and code points above U+10FFFF are rejected.
  std::size_t utf8_sequence_length() const noexcept {
    unsigned char lead = peek();
    std::size_t len = 0;
    unsigned char lo = 0x80, hi = 0xBF; // allowed range of the second byte
    if (lead >= 0xC2 && lead <= 0xDF) {
      len = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      len = 3;
      if (lead == 0xE0) lo = 0xA0;
      if (lead == 0xED) hi = 0x9F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      len = 4;
      if (lead == 0xF0) lo = 0x90;
      if (lead == 0xF4) hi = 0x8F;
    } else {
      return 0;
    }
    if (pos_ + len > text_.size()) return 0;
    for (std::size_t i = 1; i < len; ++i) {
      auto b = static_cast<unsigned char>(text_[pos_ + i]);
      if (b < (i == 1 ? lo : 0x80) || b > (i == 1 ? hi : 0xBF)) return 0;
    }
    return len;
  }

  // Precondition: peek() is the opening quote.
  bool parse_string(std::string& out) {
    std::size_t start = pos_;
    ++pos_;
    for (;;) {
      if (at_end()) return fail(start, "unterminated string");
      unsigned char c = peek();
      if (c == '"') {
        ++pos_;
        return true;
      }
      if (c == '\\') {
        if (!parse_escape(out)) return false;
        continue;
      }
      if (c < 0x20) {
        return fail(pos_, "raw control character in string (escape it, e.g. \\u00XX)");
      }
      if (c < 0x80) {
        out += static_cast<char>(c);
        ++pos_;
        continue;
      }
      std::size_t len = utf8_sequence_length();
      if (len == 0) return fail(pos_, "invalid UTF-8 in string");
      out.append(text_.substr(pos_, len));
      pos_ += len;
    }
  }

  // Precondition: peek() is the backslash.
  bool parse_escape(std::string& out) {
    std::size_t esc = pos_;
    char simple = 0;
    switch (pos_ + 1 < text_.size() ? text_[pos_ + 1] : '\0') {
    case '"': simple = '"'; break;
    case '\\': simple = '\\'; break;
    case '/': simple = '/'; break;
    case 'b': simple = '\b'; break;
    case 'f': simple = '\f'; break;
    case 'n': simple = '\n'; break;
    case 'r': simple = '\r'; break;
    case 't': simple = '\t'; break;
    case 'u': break;
    default: return fail(esc, "invalid escape sequence");
    }
    if (simple) {
      out += simple;
      pos_ += 2;
      return true;
    }

    auto cp = read_u_escape(esc);
    if (!cp) return fail(esc, "invalid \\u escape, expected 4 hex digits");
    pos_ += 6;
    if (*cp >= 0xDC00 && *cp <= 0xDFFF) return fail(esc, "lone low surrogate in \\u escape");
    if (*cp >= 0xD800 && *cp <= 0xDBFF) {
      auto low = read_u_escape(pos_);
      if (!low || *low < 0xDC00 || *low > 0xDFFF) {
        return fail(esc, "lone high surrogate in \\u escape (needs a following \\uDC00-\\uDFFF)");
      }
      pos_ += 6;
      *cp = 0x10000 + ((*cp - 0xD800) << 10) + (*low - 0xDC00);
    }
    append_utf8(out, *cp);
    return true;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  std::optional<JsonError> error_;
};

void dump_string(std::string& out, std::string_view s) {
  static constexpr char kHex[] = "0123456789abcdef";
  out += '"';
  for (char ch : s) {
    auto c = static_cast<unsigned char>(ch);
    if (c == '"') {
      out += "\\\"";
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c < 0x20) {
      out += "\\u00";
      out += kHex[c >> 4];
      out += kHex[c & 0xF];
    } else {
      out += ch;
    }
  }
  out += '"';
}

} // namespace

std::variant<Json, JsonError> Json::parse(std::string_view text) {
  return Parser(text).run();
}

std::string Json::dump() const {
  std::string out;
  dump_to(out);
  return out;
}

void Json::dump_to(std::string& out) const {
  std::visit(
      [&out](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
          out += "null";
        } else if constexpr (std::is_same_v<T, bool>) {
          out += x ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
          char buf[24];
          auto r = std::to_chars(buf, buf + sizeof buf, x);
          out.append(buf, r.ptr);
        } else if constexpr (std::is_same_v<T, double>) {
          if (!std::isfinite(x)) {
            out += "null";
            return;
          }
          char buf[32];
          auto r = std::to_chars(buf, buf + sizeof buf, x);
          std::string_view text(buf, static_cast<std::size_t>(r.ptr - buf));
          out += text;
          // "2" would re-parse as an int.
          if (text.find_first_of(".eE") == std::string_view::npos) out += ".0";
        } else if constexpr (std::is_same_v<T, std::string>) {
          dump_string(out, x);
        } else if constexpr (std::is_same_v<T, Array>) {
          out += '[';
          for (std::size_t i = 0; i < x.size(); ++i) {
            if (i) out += ',';
            x[i].dump_to(out);
          }
          out += ']';
        } else {
          static_assert(std::is_same_v<T, Object>);
          out += '{';
          for (std::size_t i = 0; i < x.size(); ++i) {
            if (i) out += ',';
            dump_string(out, x[i].first);
            out += ':';
            x[i].second.dump_to(out);
          }
          out += '}';
        }
      },
      v_);
}

bool Json::as_bool(bool def) const noexcept {
  const bool* b = std::get_if<bool>(&v_);
  return b ? *b : def;
}

std::int64_t Json::as_int(std::int64_t def) const noexcept {
  const std::int64_t* i = std::get_if<std::int64_t>(&v_);
  return i ? *i : def;
}

double Json::as_double(double def) const noexcept {
  if (const double* d = std::get_if<double>(&v_)) return *d;
  if (const std::int64_t* i = std::get_if<std::int64_t>(&v_)) return static_cast<double>(*i);
  return def;
}

std::string_view Json::as_string(std::string_view def) const noexcept {
  const std::string* s = std::get_if<std::string>(&v_);
  return s ? std::string_view(*s) : def;
}

const Json* Json::find(std::string_view key) const noexcept {
  const Object* obj = as_object();
  if (!obj) return nullptr;
  for (const auto& [k, v] : *obj) {
    if (k == key) return &v;
  }
  return nullptr;
}

Json* Json::find(std::string_view key) noexcept {
  return const_cast<Json*>(std::as_const(*this).find(key));
}

Json& Json::operator[](std::string_view key) {
  if (Json* existing = find(key)) return *existing;
  if (!is_object()) v_ = Object{};
  Object& obj = std::get<Object>(v_);
  obj.emplace_back(std::string(key), Json{});
  return obj.back().second;
}

void Json::push_back(Json value) {
  if (!is_array()) v_ = Array{};
  std::get<Array>(v_).push_back(std::move(value));
}

} // namespace asynctp::lsp
