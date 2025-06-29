# Makefile for JaiScript Foundry Tests

CXX = g++
CXXFLAGS = -std=c++20 -I../../include -Wall -Wextra -g -pthread
LDFLAGS = -pthread

# Find all test source files
CORE_SRCS = $(wildcard core/*.cpp)
LANG_SRCS = $(wildcard language/*.cpp)
INTEG_SRCS = $(wildcard integration/*.cpp)
PERF_SRCS = $(wildcard performance/*.cpp)

# Main runner
MAIN_SRC = run_tests.cpp

# JaiScript implementation files
IMPL_DIR = ../../src/implementation
IMPL_SRCS = $(IMPL_DIR)/engine.cpp $(IMPL_DIR)/lexer.cpp $(IMPL_DIR)/parser.cpp $(IMPL_DIR)/interpreter.cpp $(IMPL_DIR)/interpreter_dispatch.cpp $(IMPL_DIR)/value.cpp $(IMPL_DIR)/type_info.cpp $(IMPL_DIR)/types.cpp $(IMPL_DIR)/virtual_machine.cpp $(IMPL_DIR)/vm_backend.cpp $(IMPL_DIR)/compiler.cpp

# All sources
SRCS = $(MAIN_SRC) $(CORE_SRCS) $(LANG_SRCS) $(INTEG_SRCS) $(PERF_SRCS) $(IMPL_SRCS)
OBJS = $(SRCS:.cpp=.o)

# Output binary
TARGET = quench

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
	find . -name "*.o" -delete

# Build and run specific test in isolation
# Usage: make isolated-core/test_name.cpp
isolated-%: 
	@echo "Building isolated test: $*"
	$(CXX) $(CXXFLAGS) -DJAI_ISOLATED_TEST -o test_isolated_$* $* $(IMPL_SRCS)
	./test_isolated_$*
	rm -f test_isolated_$*

# Alternative: Build all tests in a directory as isolated executables
# This avoids .o file conflicts by creating separate executables
isolated-build-%:
	@echo "Building isolated tests in $* directory"
	@for test in $*/*.cpp; do \
		if [ -f "$$test" ]; then \
			testname=$$(basename $$test .cpp); \
			echo "Building $$testname..."; \
			$(CXX) $(CXXFLAGS) -DJAI_ISOLATED_TEST -o isolated_$$testname $$test $(IMPL_SRCS); \
		fi \
	done

# Clean isolated test executables
clean-isolated:
	rm -f test_isolated_* isolated_*