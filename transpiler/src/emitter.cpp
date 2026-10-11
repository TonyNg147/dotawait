#include "asynctp/emitter.hpp"

#include "asynctp/lowering.hpp"

namespace asynctp {

namespace {

std::size_t line_number_at(std::string_view source, std::size_t offset) {
  std::size_t line = 1;
  std::size_t limit = offset < source.size() ? offset : source.size();
  for (std::size_t i = 0; i < limit; ++i) {
    if (source[i] == '\n') line++;
  }
  return line;
}

} // namespace

std::variant<std::string, ParseError> emit(const ParsedFile& parsed, const std::string& filename,
                                            bool with_line_directives, bool with_runtime_include) {
  std::string out;
  out.reserve(parsed.source.size() + 64);
  std::size_t cursor = 0;

  if (with_runtime_include && !parsed.functions.empty()) {
    out += "#include <asyncrt/asyncrt.hpp>\n";
  }
  if (with_line_directives) {
    out += "#line 1 \"" + filename + "\"\n";
  }

  for (const auto& fn : parsed.functions) {
    out.append(parsed.source.substr(cursor, fn.async_kw_offset - cursor));

    auto lowered = lower_function(fn);
    if (std::holds_alternative<ParseError>(lowered)) return std::get<ParseError>(lowered);
    out.append(std::get<std::string>(lowered));

    cursor = fn.body_close_offset + 1;

    if (with_line_directives) {
      std::size_t ln = line_number_at(parsed.source, cursor);
      out += "\n#line " + std::to_string(ln) + " \"" + filename + "\"\n";
    }
  }

  out.append(parsed.source.substr(cursor));
  return out;
}

} // namespace asynctp
