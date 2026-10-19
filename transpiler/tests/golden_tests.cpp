// Runs the real `asynctp` binary (as a subprocess) against each
// tests/golden/*.async.cpp fixture and diffs its output byte-for-byte
// against the matching *.expected.cpp, with --no-line-directives for
// stability. This is the one test that exercises the actual CLI tool, not
// just the library functions it's built from.
//
// Usage: golden_tests <path-to-asynctp> <path-to-golden-dir>

#include "test_support.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool run_asynctp(const std::string& asynctp_path, const std::string& input, const std::string& output) {
  std::string cmd = asynctp_path + " " + input + " -o " + output + " --no-line-directives";
  return std::system(cmd.c_str()) == 0;
}

void run_case(const std::string& asynctp_path, const std::string& golden_dir, const std::string& case_name) {
  std::string input = golden_dir + "/" + case_name + ".async.cpp";
  std::string expected_path = golden_dir + "/" + case_name + ".expected.cpp";
  std::string actual_path = "/tmp/asynctp_golden_" + case_name + ".cpp";

  bool ran_ok = run_asynctp(asynctp_path, input, actual_path);
  CHECK(ran_ok);
  if (!ran_ok) return;

  std::string expected = read_file(expected_path);
  std::string actual = read_file(actual_path);
  if (expected != actual) {
    asynctp::test::check(false, "golden mismatch for " + case_name, __FILE__, __LINE__);
    std::cerr << "--- expected (" << expected_path << ") ---\n" << expected << "\n";
    std::cerr << "--- actual (" << actual_path << ") ---\n" << actual << "\n";
  } else {
    asynctp::test::check(true, "golden match for " + case_name, __FILE__, __LINE__);
  }
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: golden_tests <path-to-asynctp> <path-to-golden-dir>\n";
    return 2;
  }
  std::string asynctp_path = argv[1];
  std::string golden_dir = argv[2];

  run_case(asynctp_path, golden_dir, "simple_fn");
  run_case(asynctp_path, golden_dir, "nested_lambda");
  run_case(asynctp_path, golden_dir, "digit_separators");
  run_case(asynctp_path, golden_dir, "literals_around_signature");
  run_case(asynctp_path, golden_dir, "raw_string_in_define");
  run_case(asynctp_path, golden_dir, "braceless_return");

  return asynctp::test::report("golden_tests");
}
