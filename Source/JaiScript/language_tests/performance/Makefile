# Makefile for JaiScript Performance Tests

CXX = g++
CXXFLAGS = -std=c++20 -I../../../include -Wall -Wextra -O3 -march=native
LDFLAGS = -pthread

# JaiScript implementation files
IMPL_DIR = ../../../src/implementation
IMPL_SRCS = $(IMPL_DIR)/engine.cpp \
            $(IMPL_DIR)/lexer.cpp \
            $(IMPL_DIR)/parser.cpp \
            $(IMPL_DIR)/interpreter.cpp \
            $(IMPL_DIR)/interpreter_dispatch.cpp \
            $(IMPL_DIR)/value.cpp \
            $(IMPL_DIR)/type_info.cpp \
            $(IMPL_DIR)/types.cpp \
            $(IMPL_DIR)/conversion_registry.cpp \
            $(IMPL_DIR)/class_registry.cpp \
            $(IMPL_DIR)/vm_backend_stub.cpp

# ChaiScript include path (if available)
CHAISCRIPT_INCLUDE = -I../../../../../External/ChaiScript-6.1.0/include

# Performance test executables
TARGETS = script_class_perf native_class_test comprehensive_benchmark

.PHONY: all clean run

all: $(TARGETS)

# Build updated script class performance test
script_class_perf: script_class_vs_chaiscript_perf.cpp $(IMPL_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Build native class test
native_class_test: test_native_classes.cpp $(IMPL_SRCS)
	$(CXX) $(CXXFLAGS) -DJAI_ISOLATED_TEST -o $@ $^ $(LDFLAGS)

# Build comprehensive benchmark (with ChaiScript if available)
comprehensive_benchmark: comprehensive_benchmark.cpp $(IMPL_SRCS)
	$(CXX) $(CXXFLAGS) $(CHAISCRIPT_INCLUDE) -DHAS_CHAISCRIPT -o $@ $^ $(LDFLAGS)

# Run all benchmarks
run: all
	@echo "=== Running Script Class Performance Test ==="
	./script_class_perf
	@echo ""
	@echo "=== Running Native Class Test ==="
	./native_class_test
	@echo ""
	@echo "=== Running Comprehensive Benchmark ==="
	./comprehensive_benchmark

# Run individual tests
run-script: script_class_perf
	./script_class_perf

run-native: native_class_test
	./native_class_test

run-comprehensive: comprehensive_benchmark
	./comprehensive_benchmark

clean:
	rm -f $(TARGETS) *.o

# Build without ChaiScript (fallback)
comprehensive_benchmark_no_chai: comprehensive_benchmark.cpp $(IMPL_SRCS)
	$(CXX) $(CXXFLAGS) -o comprehensive_benchmark $^ $(LDFLAGS)

# Quick performance test
quick: script_class_perf
	./script_class_perf

.PHONY: help
help:
	@echo "Available targets:"
	@echo "  make all                    - Build all performance tests"
	@echo "  make script_class_perf      - Build script class performance test"
	@echo "  make native_class_test      - Build native class test"
	@echo "  make comprehensive_benchmark - Build comprehensive benchmark"
	@echo "  make run                    - Run all tests"
	@echo "  make run-script             - Run script class test only"
	@echo "  make run-native             - Run native class test only"
	@echo "  make run-comprehensive      - Run comprehensive benchmark only"
	@echo "  make quick                  - Quick performance test"
	@echo "  make clean                  - Clean build artifacts"