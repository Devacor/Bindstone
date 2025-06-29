#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <vector>

using namespace jai;
using namespace jai::test;
using namespace std::chrono;

JAI_TEST_SUITE(BinaryExprBenchmark)

// Helper to run a benchmark and return microseconds
template<typename F>
double benchmark_operation(const std::string& name, int iterations, F&& func) {
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        func();
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    double per_iteration = static_cast<double>(duration) / iterations;
    
    std::cout << std::setw(35) << std::left << name 
              << std::setw(10) << std::right << std::fixed << std::setprecision(2) 
              << per_iteration << " μs/op" << std::endl;
    
    return per_iteration;
}

// Test integer arithmetic operations
JAI_TEST(integer_arithmetic_benchmark) {
    std::cout << "\n=== Integer Arithmetic Operations ===" << std::endl;
    
    jai::engine engine;
    const int iterations = 10000;
    
    // Prepare variables to avoid parsing overhead
    engine.execute("var a = 42; var b = 17;");
    
    benchmark_operation("Integer addition (a + b)", iterations, [&]() {
        engine.execute("a + b;");
    });
    
    benchmark_operation("Integer subtraction (a - b)", iterations, [&]() {
        engine.execute("a - b;");
    });
    
    benchmark_operation("Integer multiplication (a * b)", iterations, [&]() {
        engine.execute("a * b;");
    });
    
    benchmark_operation("Integer division (a / b)", iterations, [&]() {
        engine.execute("a / b;");
    });
    
    expect_true(true); // Always pass - this is a benchmark
}

// Test floating point arithmetic operations
JAI_TEST(float_arithmetic_benchmark) {
    std::cout << "\n=== Float Arithmetic Operations ===" << std::endl;
    
    jai::engine engine;
    const int iterations = 10000;
    
    // Prepare variables
    engine.execute("var x = 42.5; var y = 17.3;");
    
    benchmark_operation("Float addition (x + y)", iterations, [&]() {
        engine.execute("x + y;");
    });
    
    benchmark_operation("Float subtraction (x - y)", iterations, [&]() {
        engine.execute("x - y;");
    });
    
    benchmark_operation("Float multiplication (x * y)", iterations, [&]() {
        engine.execute("x * y;");
    });
    
    benchmark_operation("Float division (x / y)", iterations, [&]() {
        engine.execute("x / y;");
    });
    
    expect_true(true);
}

// Test comparison operations
JAI_TEST(comparison_benchmark) {
    std::cout << "\n=== Comparison Operations ===" << std::endl;
    
    jai::engine engine;
    const int iterations = 10000;
    
    engine.execute("var a = 42; var b = 17;");
    
    benchmark_operation("Integer less than (a < b)", iterations, [&]() {
        engine.execute("a < b;");
    });
    
    benchmark_operation("Integer equality (a == b)", iterations, [&]() {
        engine.execute("a == b;");
    });
    
    benchmark_operation("Integer inequality (a != b)", iterations, [&]() {
        engine.execute("a != b;");
    });
    
    benchmark_operation("Integer spaceship (a <=> b)", iterations, [&]() {
        engine.execute("a <=> b;");
    });
    
    expect_true(true);
}

// Test bitwise operations
JAI_TEST(bitwise_benchmark) {
    std::cout << "\n=== Bitwise Operations ===" << std::endl;
    
    jai::engine engine;
    const int iterations = 10000;
    
    engine.execute("var a = 255; var b = 170;");
    
    benchmark_operation("Bitwise AND (a & b)", iterations, [&]() {
        engine.execute("a & b;");
    });
    
    benchmark_operation("Bitwise OR (a | b)", iterations, [&]() {
        engine.execute("a | b;");
    });
    
    benchmark_operation("Bitwise XOR (a ^ b)", iterations, [&]() {
        engine.execute("a ^ b;");
    });
    
    benchmark_operation("Left shift (a << 2)", iterations, [&]() {
        engine.execute("a << 2;");
    });
    
    expect_true(true);
}

// Test mixed type operations
JAI_TEST(mixed_type_benchmark) {
    std::cout << "\n=== Mixed Type Operations ===" << std::endl;
    
    jai::engine engine;
    const int iterations = 10000;
    
    engine.execute("var i = 42; var f = 3.14;");
    
    benchmark_operation("Int + Float (i + f)", iterations, [&]() {
        engine.execute("i + f;");
    });
    
    benchmark_operation("Float + Int (f + i)", iterations, [&]() {
        engine.execute("f + i;");
    });
    
    expect_true(true);
}

// Test string operations
JAI_TEST(string_operations_benchmark) {
    std::cout << "\n=== String Operations ===" << std::endl;
    
    jai::engine engine;
    const int iterations = 10000;
    
    engine.execute("var s1 = \"Hello, \"; var s2 = \"World!\";");
    
    benchmark_operation("String concatenation (s1 + s2)", iterations, [&]() {
        engine.execute("s1 + s2;");
    });
    
    benchmark_operation("String comparison (s1 < s2)", iterations, [&]() {
        engine.execute("s1 < s2;");
    });
    
    expect_true(true);
}

// Test complex expressions
JAI_TEST(complex_expression_benchmark) {
    std::cout << "\n=== Complex Expressions ===" << std::endl;
    
    jai::engine engine;
    const int iterations = 5000;
    
    engine.execute("var a = 10; var b = 20; var c = 30; var d = 40;");
    
    benchmark_operation("Chain arithmetic (a+b+c+d)", iterations, [&]() {
        engine.execute("a + b + c + d;");
    });
    
    benchmark_operation("Mixed ops ((a+b)*(c-d))", iterations, [&]() {
        engine.execute("(a + b) * (c - d);");
    });
    
    benchmark_operation("Complex ((a*b+c)/d)", iterations, [&]() {
        engine.execute("(a * b + c) / d;");
    });
    
    expect_true(true);
}

// Test with custom operator overloading
JAI_TEST(custom_operator_benchmark) {
    std::cout << "\n=== Custom Operator Performance ===" << std::endl;
    
    jai::engine engine;
    const int iterations = 5000;
    
    // Define a custom + operator for demonstration
    engine.add_function("+", [](int a, int b) { return a + b + 1000; }); // Custom behavior
    engine.execute("var x = 5; var y = 10;");
    
    benchmark_operation("Custom operator (x + y)", iterations, [&]() {
        engine.execute("x + y;");
    });
    
    // Compare with built-in for other types
    engine.execute("var f1 = 5.5; var f2 = 10.5;");
    
    benchmark_operation("Built-in float + (f1 + f2)", iterations, [&]() {
        engine.execute("f1 + f2;");
    });
    
    expect_true(true);
}

// Aggregate results summary
JAI_TEST(performance_summary) {
    std::cout << "\n=== Performance Summary ===" << std::endl;
    std::cout << "This benchmark measures the performance of binary expression evaluation." << std::endl;
    std::cout << "The visit_binary_expr method is currently 800+ lines with many branches." << std::endl;
    std::cout << "Optimization opportunities:" << std::endl;
    std::cout << "1. Dispatch table for operators instead of string comparisons" << std::endl;
    std::cout << "2. Fast paths for common numeric operations" << std::endl;
    std::cout << "3. Avoid repeated type checking" << std::endl;
    std::cout << "4. Cache operator lookups" << std::endl;
    
    expect_true(true);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()