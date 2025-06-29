CXX = g++
CXXFLAGS = -std=c++20 -I. -I../../include -g -O2 -Wall -Wextra
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
               $(SRC_DIR)/interpreter_dispatch.cpp \
               $(SRC_DIR)/engine.cpp \
               $(SRC_DIR)/virtual_machine.cpp \
               $(SRC_DIR)/compiler.cpp \
               $(SRC_DIR)/vm_backend.cpp

# Core test sources
CORE_TESTS = test_lexer test_parser test_value test_engine test_type_safety test_type_conversions \
             test_error_handling test_execute_return_value

# Operator tests
OPERATOR_TESTS = test_operators test_operator_overloading test_spaceship_custom_types \
                 test_simple_op test_simple_operator test_extended_operators \
                 test_arithmetic_simple

# Class and C++ binding tests
CLASS_TESTS = test_class_builder test_cpp_bindings test_class_instantiation \
              test_class_methods test_class_builder_minimal test_class_builder_comprehensive

# Function and lambda tests
FUNCTION_TESTS = test_functions test_lambdas test_lambda_captures test_lambda_syntax test_syntax \
                 test_function_params test_function_debug_simple test_function_recursion_debug \
                 test_script_references

# Control flow tests
CONTROL_TESTS = test_control_flow test_range_for test_recursion_simple test_nested_loops

# Utility tests
UTILITY_TESTS = test_extraction_convenience test_parameter_convenience \
                test_variable_query test_return_values test_float_conversion \
                test_custom_type_return test_json test_json_cpp_objects

# Container and special feature tests
FEATURE_TESTS = test_array_assignment test_subscript test_nested_containers \
                test_reference_types test_function_registration_clarity \
                test_function_registration_examples test_custom_subscript_operator \
                test_debug_subscript test_template_types test_array_methods test_map_methods

# Debug tests (for debugging specific features)
DEBUG_TESTS = test_debug_simple test_debug_class test_conditional_debug

# Memory and performance tests (optional, can be run separately)
MEMORY_TESTS = test_memory_leak_detection test_memory_leak_specific \
               test_memory_stress test_memory_stress_simple

PERF_TESTS = test_performance test_performance_summary test_jaiscript_vs_chaiscript \
            test_jaiscript_vs_squirrel test_arithmetic_perf_baseline test_variable_perf_baseline \
            test_variable_access_perf test_function_call_optimization test_parameter_binding_optimization \
            test_value_copy_optimization test_array_methods_perf test_binary_expr_benchmark \
            test_binary_expr_optimized

# VM tests
VM_TESTS = test_vm test_vm_script_features test_vm_debug test_vm_operators test_vm_strings \
           test_vm_error_handling test_vm_closures test_vm_cpp_integration test_backend_comparison \
           test_vm_compiler_basic

# All regular tests (excluding memory and performance)
ALL_TESTS = $(CORE_TESTS) $(OPERATOR_TESTS) $(CLASS_TESTS) $(FUNCTION_TESTS) \
            $(CONTROL_TESTS) $(UTILITY_TESTS) $(FEATURE_TESTS) $(VM_TESTS)

# Object files
IMPL_OBJECTS = $(IMPL_SOURCES:.cpp=.o)

# Output directory
BIN_DIR = bin

# Build all test executables
TEST_EXECUTABLES = $(patsubst %,$(BIN_DIR)/%,$(ALL_TESTS))

# Backend comparison tool
TOOL_BACKEND_COMPARISON = $(BIN_DIR)/tool_backend_comparison

# Default target
all: $(BIN_DIR) $(TEST_EXECUTABLES) $(TOOL_BACKEND_COMPARISON)

# Create bin directory
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Generic rule to build any test
$(BIN_DIR)/test_%: $(TEST_DIR)/test_%.cpp $(IMPL_OBJECTS)
	$(CXX) $(CXXFLAGS) $< $(IMPL_OBJECTS) -o $@ $(LDFLAGS)

# Build backend comparison tool
$(TOOL_BACKEND_COMPARISON): tool_backend_comparison.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

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

# Run debug tests
test-debug: $(patsubst %,$(BIN_DIR)/%,$(DEBUG_TESTS))
	@for test in $(DEBUG_TESTS); do \
		echo "\n=== $$test ==="; \
		$(BIN_DIR)/$$test || true; \
	done

# Run VM tests
test-vm: $(patsubst %,$(BIN_DIR)/%,$(VM_TESTS))
	@for test in $(VM_TESTS); do \
		echo "\n=== $$test ==="; \
		$(BIN_DIR)/$$test || true; \
	done

# Compare backends
compare-backends: all
	@echo "Comparing interpreter vs JVM backends..."
	@$(TOOL_BACKEND_COMPARISON)

# Compare specific tests
compare-test: all
	@if [ -z "$(TEST)" ]; then \
		echo "Usage: make compare-test TEST=test_name"; \
		exit 1; \
	fi
	@$(TOOL_BACKEND_COMPARISON) $(BIN_DIR)/$(TEST)

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
.PHONY: all clean test test-verbose test-core test-memory test-perf test-debug test-vm compare-backends compare-test rebuild help

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
	@echo "  test-debug   - Run debug/diagnostic tests"
	@echo "  test-vm      - Run VM-specific tests"
	@echo "  compare-backends - Compare all tests between interpreter and JVM"
	@echo "  compare-test TEST=name - Compare specific test between backends"
	@echo "  clean        - Remove all build artifacts"
	@echo "  rebuild      - Clean and build everything"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Individual tests can be run with: ./bin/test_name"