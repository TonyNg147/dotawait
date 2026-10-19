# Plain-Makefile build, used until `cmake` is installed (see CMakeLists.txt
# for the real build integration this mirrors: transpiler/, runtime/,
# examples/, wired together the same way cmake/AsyncTranspile.cmake does).
#
# Targets:
#   make            build asynctp, all test binaries, and the examples
#   make test       build everything and run every test suite + example
#   make clean      remove build/

CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra
BUILD := build

TRANSPILER_SRCS := transpiler/src/scanner.cpp transpiler/src/tokenizer.cpp transpiler/src/lexer.cpp \
                    transpiler/src/async_parser.cpp transpiler/src/lowering.cpp \
                    transpiler/src/emitter.cpp \
                    transpiler/src/lsp/json.cpp transpiler/src/lsp/framing.cpp
TRANSPILER_INC := -Itranspiler/include
RUNTIME_INC := -Iruntime/include

ASYNCTP := $(BUILD)/asynctp

# Adding an example is just: drop examples/<name>.async.cpp and list <name> here.
EXAMPLES := hello yielding braceless_return
EXAMPLE_BINS := $(EXAMPLES:%=$(BUILD)/%_example)

.PHONY: all test clean

all: $(ASYNCTP) \
     $(BUILD)/tokenizer_tests $(BUILD)/lexer_tests $(BUILD)/parser_tests $(BUILD)/lowering_tests $(BUILD)/golden_tests \
     $(BUILD)/json_tests $(BUILD)/framing_tests \
     $(BUILD)/task_tests $(BUILD)/executor_tests \
     $(EXAMPLE_BINS)

$(BUILD):
	mkdir -p $(BUILD)/generated

$(ASYNCTP): transpiler/src/main.cpp $(TRANSPILER_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TRANSPILER_INC) -o $@ $^

$(BUILD)/tokenizer_tests: transpiler/tests/tokenizer_tests.cpp $(TRANSPILER_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TRANSPILER_INC) -o $@ $^

$(BUILD)/lexer_tests: transpiler/tests/lexer_tests.cpp $(TRANSPILER_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TRANSPILER_INC) -o $@ $^

$(BUILD)/parser_tests: transpiler/tests/parser_tests.cpp $(TRANSPILER_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TRANSPILER_INC) -o $@ $^

$(BUILD)/lowering_tests: transpiler/tests/lowering_tests.cpp $(TRANSPILER_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TRANSPILER_INC) -o $@ $^

$(BUILD)/json_tests: transpiler/tests/json_tests.cpp $(TRANSPILER_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TRANSPILER_INC) -o $@ $^

$(BUILD)/framing_tests: transpiler/tests/framing_tests.cpp $(TRANSPILER_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TRANSPILER_INC) -o $@ $^

$(BUILD)/golden_tests: transpiler/tests/golden_tests.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TRANSPILER_INC) -o $@ $^

$(BUILD)/task_tests: runtime/tests/task_tests.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(RUNTIME_INC) -o $@ $^

$(BUILD)/executor_tests: runtime/tests/executor_tests.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(RUNTIME_INC) -o $@ $^

$(BUILD)/generated/%.cpp: examples/%.async.cpp $(ASYNCTP) | $(BUILD)
	$(ASYNCTP) $< -o $@

$(BUILD)/%_example: $(BUILD)/generated/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(RUNTIME_INC) -Iexamples -o $@ $<

# Keep the transpiled .cpp around for inspection.
.PRECIOUS: $(BUILD)/generated/%.cpp

test: all
	$(BUILD)/tokenizer_tests
	$(BUILD)/lexer_tests
	$(BUILD)/parser_tests
	$(BUILD)/lowering_tests
	$(BUILD)/golden_tests $(ASYNCTP) transpiler/tests/golden
	@for f in transpiler/tests/golden/*.expected.cpp; do \
	  echo "golden-compile $$f"; \
	  $(CXX) -std=c++20 -fsyntax-only $(RUNTIME_INC) $$f || exit 1; \
	done
	$(BUILD)/json_tests
	$(BUILD)/framing_tests
	$(BUILD)/task_tests
	$(BUILD)/executor_tests
	@for e in $(EXAMPLE_BINS); do echo $$e; $$e || exit 1; done

clean:
	rm -rf $(BUILD)
