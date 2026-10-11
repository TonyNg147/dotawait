#include "asynctp/async_parser.hpp"
#include "asynctp/emitter.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

void print_usage() {
  std::cerr << "usage: asynctp <input.async.cpp> -o <output.cpp> [--line-directives] "
               "[--no-line-directives] [--no-runtime-include] [--verbose]\n";
}

} // namespace

int main(int argc, char** argv) {
  std::string input_path;
  std::string output_path;
  bool with_line_directives = true;
  bool with_runtime_include = true;
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-o" && i + 1 < argc) {
      output_path = argv[++i];
    } else if (arg == "--line-directives") {
      with_line_directives = true;
    } else if (arg == "--no-line-directives") {
      with_line_directives = false;
    } else if (arg == "--no-runtime-include") {
      with_runtime_include = false;
    } else if (arg == "--verbose") {
      verbose = true;
    } else if (!arg.empty() && arg[0] != '-') {
      input_path = arg;
    } else {
      std::cerr << "asynctp: unrecognized option '" << arg << "'\n";
      print_usage();
      return 1;
    }
  }

  if (input_path.empty() || output_path.empty()) {
    print_usage();
    return 1;
  }

  std::string source;
  if (!read_file(input_path, source)) {
    std::cerr << input_path << ": error: could not read input file\n";
    return 1;
  }
  if (verbose) std::cerr << "asynctp: read " << source.size() << " bytes from " << input_path << "\n";

  auto parsed = asynctp::parse_file(source);
  if (std::holds_alternative<asynctp::ParseError>(parsed)) {
    const auto& e = std::get<asynctp::ParseError>(parsed);
    std::cerr << input_path << ":" << e.line << ":" << e.col << ": error: " << e.message << "\n";
    return 1;
  }
  const auto& parsed_file = std::get<asynctp::ParsedFile>(parsed);
  if (verbose) std::cerr << "asynctp: found " << parsed_file.functions.size() << " async function(s)\n";

  auto emitted = asynctp::emit(parsed_file, input_path, with_line_directives, with_runtime_include);
  if (std::holds_alternative<asynctp::ParseError>(emitted)) {
    const auto& e = std::get<asynctp::ParseError>(emitted);
    std::cerr << input_path << ":" << e.line << ":" << e.col << ": error: " << e.message << "\n";
    return 1;
  }

  // Everything succeeded before any output file is touched, so a failed run
  // never leaves a stale-but-newer output confusing make/ninja.
  std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    std::cerr << output_path << ": error: could not open output file for writing\n";
    return 1;
  }
  out << std::get<std::string>(emitted);
  return 0;
}
