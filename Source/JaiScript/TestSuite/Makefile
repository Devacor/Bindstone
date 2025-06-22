CXX = g++
CXXFLAGS = -std=c++20 -I. -I../include -g -O2 -Wall -Wextra
LDFLAGS = 

# Source directories
SRC_DIR = ../src/implementation
TEST_DIR = Tests

# Implementation sources
IMPL_SOURCES = $(SRC_DIR)/lexer.cpp \
               $(SRC_DIR)/parser.cpp \
               $(SRC_DIR)/type_info.cpp \
               $(SRC_DIR)/value.cpp \
               $(SRC_DIR)/interpreter.cpp \
               $(SRC_DIR)/engine.cpp

# Core test sources
CORE_TESTS = test_lexer test_parser test_value test_engine test_type_safety test_type_conversions

# Operator tests
OPERATOR_TESTS = test_operators test_operator_overloading test_spaceship_custom_types \
                 test_simple_op test_simple_operator test_extended_operators

# Class and C++ binding tests
CLASS_TESTS = test_class_builder test_cpp_bindings test_class_instantiation \
              test_class_methods test_class_builder_minimal test_class_builder_comprehensive

# Function and lambda tests
FUNCTION_TESTS = test_functions test_lambdas test_lambda_captures

# Control flow tests
CONTROL_TESTS = test_control_flow test_range_for

# Utility tests
UTILITY_TESTS = test_extraction_convenience test_parameter_convenience \
                test_variable_query test_return_values test_float_conversion

# Container and special feature tests
FEATURE_TESTS = test_array_assignment test_subscript test_nested_containers \
                test_reference_types test_function_registration_clarity \
                test_function_registration_examples

# Memory and performance tests (optional, can be run separately)
MEMORY_TESTS = test_memory_leak_detection test_memory_leak_specific \
               test_memory_stress test_memory_stress_simple

PERF_TESTS = test_performance test_performance_summary test_jaiscript_vs_chaiscript

# All regular tests (excluding memory and performance)
ALL_TESTS = $(CORE_TESTS) $(OPERATOR_TESTS) $(CLASS_TESTS) $(FUNCTION_TESTS) \
            $(CONTROL_TESTS) $(UTILITY_TESTS) $(FEATURE_TESTS)

# Object files
IMPL_OBJECTS = $(IMPL_SOURCES:.cpp=.o)

# Output directory
BIN_DIR = bin

# Build all test executables
TEST_EXECUTABLES = $(patsubst %,$(BIN_DIR)/%,$(ALL_TESTS))

# Default target
all: $(BIN_DIR) $(TEST_EXECUTABLES)

# Create bin directory
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Generic rule to build any test
$(BIN_DIR)/test_%: $(TEST_DIR)/test_%.cpp $(IMPL_OBJECTS)
	$(CXX) $(CXXFLAGS) $< $(IMPL_OBJECTS) -o $@ $(LDFLAGS)

# Run all tests
test: all
	@echo "Running all tests..."
	@failed=0; \
	for test in $(ALL_TESTS); do \
		echo "\n=== $$test ==="; \
		if $(BIN_DIR)/$$test; then \
			echo "✓ $$test passed"; \
		else \
			echo "✗ $$test FAILED"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "\n=== Test Summary ==="; \
	echo "Total tests: $$(echo $(ALL_TESTS) | wc -w)"; \
	echo "Failed: $$failed"; \
	exit $$failed

# Run tests with verbose output
test-verbose: all
	@for test in $(ALL_TESTS); do \
		echo "\n=== $$test (verbose) ==="; \
		$(BIN_DIR)/$$test --verbose || true; \
	done

# Run only core tests
test-core: $(patsubst %,$(BIN_DIR)/%,$(CORE_TESTS))
	@for test in $(CORE_TESTS); do \
		echo "\n=== $$test ==="; \
		$(BIN_DIR)/$$test || true; \
	done

# Run memory tests
test-memory: $(patsubst %,$(BIN_DIR)/%,$(MEMORY_TESTS))
	@for test in $(MEMORY_TESTS); do \
		echo "\n=== $$test ==="; \
		$(BIN_DIR)/$$test || true; \
	done

# Run performance tests
test-perf: $(patsubst %,$(BIN_DIR)/%,$(PERF_TESTS))
	@for test in $(PERF_TESTS); do \
		echo "\n=== $$test ==="; \
		$(BIN_DIR)/$$test || true; \
	done

# Compile pattern rules
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
clean:
	rm -f $(IMPL_OBJECTS) $(TEST_DIR)/*.o
	rm -rf $(BIN_DIR)

# Clean and rebuild
rebuild: clean all

# Phony targets
.PHONY: all clean test test-verbose test-core test-memory test-perf rebuild

# Help
help:
	@echo "JaiScript Test Suite Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build all test executables"
	@echo "  test         - Run all tests"
	@echo "  test-verbose - Run all tests with verbose output"
	@echo "  test-core    - Run only core language tests"
	@echo "  test-memory  - Run memory leak/stress tests"
	@echo "  test-perf    - Run performance benchmarks"
	@echo "  clean        - Remove all build artifacts"
	@echo "  rebuild      - Clean and build everything"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Individual tests can be run with: ./bin/test_name"