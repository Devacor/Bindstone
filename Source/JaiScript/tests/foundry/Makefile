# Makefile for JaiScript Foundry Tests

CXX = g++
CXXFLAGS = -std=c++20 -I../../include -Wall -Wextra -Wno-unused-parameter -g -pthread -O0 -fno-omit-frame-pointer -fno-inline
LDFLAGS = -pthread

# Find all test source files
CORE_SRCS = $(wildcard core/*.cpp)
LANG_SRCS = $(wildcard language/*.cpp)
# Exclude standalone test files that have their own main()
INTEG_EXCLUDE = integration/test_shared_ptr_conversion.cpp integration/test_shared_ptr_debug.cpp integration/test_operator_class_instance_fix.cpp
INTEG_SRCS = $(filter-out $(INTEG_EXCLUDE), $(wildcard integration/*.cpp))
# Performance tests are standalone, not part of the main suite
# PERF_SRCS = $(wildcard performance/*.cpp)
# These VM tests have undefined VM references (VM implementation not included in build)
VM_EXCLUDE = vm/vm_direct_function_test.cpp vm/vm_function_debug.cpp
VM_SRCS = $(filter-out $(VM_EXCLUDE), $(wildcard vm/*.cpp))

# Root-level test files that use foundry framework
ROOT_TEST_SRCS = test_vm_method_calls.cpp test_vm_references.cpp test_vm_value_semantics.cpp \
                 test_constructor_syntax_comprehensive.cpp test_map_debug.cpp

# Main runner
MAIN_SRC = run_tests.cpp

# JaiScript implementation files
IMPL_DIR = ../../src/implementation
IMPL_SRCS = $(IMPL_DIR)/engine.cpp $(IMPL_DIR)/lexer.cpp $(IMPL_DIR)/parser.cpp $(IMPL_DIR)/interpreter.cpp $(IMPL_DIR)/interpreter_dispatch.cpp $(IMPL_DIR)/value.cpp $(IMPL_DIR)/type_info.cpp $(IMPL_DIR)/types.cpp $(IMPL_DIR)/conversion_registry.cpp vm_backend_fallback.cpp

# All sources
SRCS = $(MAIN_SRC) $(CORE_SRCS) $(LANG_SRCS) $(INTEG_SRCS) $(VM_SRCS) $(IMPL_SRCS)
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