#include "asynctp/lexer.hpp"

namespace asynctp {

std::vector<std::size_t> find_async_markers(const TokenizedFile& file) {
  std::vector<std::size_t> markers;
  for (const Tok& tok : file.toks) {
    if (tok.kind == TokKind::Identifier && text(file, tok) == "async") {
      markers.push_back(tok.begin);
    }
  }
  return markers;
}

} // namespace asynctp
