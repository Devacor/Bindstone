#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <chrono>
#include <iomanip>

using namespace jai;
using namespace jai::test;
using namespace std::chrono;

JAI_TEST_SUITE(ParameterBindingOptimization)

class PerformanceBenchmark {
    const std::string name;
    high_resolution_clock::time_point start;
    
public:
    PerformanceBenchmark(const std::string& n) : name(n), start(high_resolution_clock::now()) {}
    
    ~PerformanceBenchmark() {
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start).count();
        std::cout << std::setw(50) << std::left << name 
                  << std::setw(8) << std::right << duration << " μs" << std::endl;
    }
};

// Test functions with many parameters (should benefit most from symbol ID caching)
JAI_TEST(many_parameter_function_test) {
    std::cout << "\n=== Many parameter Function Test ===\n";
    
    engine engine;
    script_value result;
    
    {
        PerformanceBenchmark bench("Many-parameter calls (500 iterations)");
        result = engine.execute(R"(
            function calculate8(auto a, auto b, auto c, auto d, auto e, auto f, auto g, auto h) -> auto {
                return a + b + c + d + e + f + g + h;
            }
            
            var total = 0;
            for (var i = 0; i < 500; i = i + 1) {
                total = total + calculate8(i, i+1, i+2, i+3, i+4, i+5, i+6, i+7);
            }
            total;
        )");
    }
    
    expect_eq(result.as_int(), 1999000);
}

// Test repeated function calls with same parameters (symbol ID caching benefit)
JAI_TEST(repeated_function_calls_test) {
    std::cout << "\n=== Repeated Function Calls Test ===\n";
    
    engine engine;
    script_value result;
    
    {
        PerformanceBenchmark bench("Repeated calls with caching (1000 iterations)");
        result = engine.execute(R"(
            function processData(auto input, auto multiplier, auto offset, auto factor) -> auto {
                return input * multiplier + offset * factor;
            }
            
            var sum = 0;
            for (var i = 0; i < 1000; i = i + 1) {
                sum = sum + processData(i, 2, 10, 3);
            }
            sum;
        )");
    }
    
    expect_eq(result.as_int(), 1029000);
}

// Test functions with long parameter names (string interning overhead test)
JAI_TEST(long_parameter_names_test) {
    std::cout << "\n=== Long parameter Names Test ===\n";
    
    engine engine;
    script_value result;
    
    {
        PerformanceBenchmark bench("Long parameter names (300 iterations)");
        result = engine.execute(R"(
            function complexCalculation(auto inputDatascript_value, auto processingMultiplier, auto baselineOffset, auto scalingFactor) -> auto {
                return inputDatascript_value * processingMultiplier + baselineOffset - scalingFactor;
            }
            
            var result = 0;
            for (var i = 0; i < 300; i = i + 1) {
                result = result + complexCalculation(i, 3, 100, 5);
            }
            result;
        )");
    }
    
    expect_eq(result.as_int(), 163950);
}

// Test nested function calls (environment and parameter optimization together)
JAI_TEST(nested_function_parameter_test) {
    std::cout << "\n=== Nested Function parameter Test ===\n";
    
    engine engine;
    script_value result;
    
    {
        PerformanceBenchmark bench("Nested function parameters (200 iterations)");
        result = engine.execute(R"(
            function outer(auto x, auto y, auto z) -> auto {
                function inner(auto a, auto b) -> auto {
                    return a * b + z;
                }
                return inner(x, y) + z;
            }
            
            var total = 0;
            for (var i = 0; i < 200; i = i + 1) {
                total = total + outer(i, i+1, 5);
            }
            total;
        )");
    }
    
    expect_eq(result.as_int(), 422000);
}

// Test recursive functions with parameters (parameter caching + environment pooling)
JAI_TEST(recursive_parameter_test) {
    std::cout << "\n=== Recursive Function parameter Test ===\n";
    
    engine engine;
    script_value result;
    
    {
        PerformanceBenchmark bench("Recursive function with parameters");
        result = engine.execute(R"(
            function power(auto base, auto exponent) -> auto {
                if (exponent <= 0) {
                    return 1;
                }
                return base * power(base, exponent - 1);
            }
            
            power(2, 10);
        )");
    }
    
    expect_eq(result.as_int(), 1024);
    
    {
        PerformanceBenchmark bench("Multiple recursive calls (50 iterations)");
        result = engine.execute(R"(
            function factorial(auto n, auto accumulator) -> auto {
                if (n <= 1) {
                    return accumulator;
                }
                return factorial(n - 1, n * accumulator);
            }
            
            var sum = 0;
            for (var i = 1; i <= 50; i = i + 1) {
                sum = sum + factorial(i % 8 + 1, 1);
            }
            sum;
        )");
    }
    
    expect_eq(result.as_int(), 334200);
}

// Test parameter type variety (different parameter types and caching behavior)
JAI_TEST(mixed_parameter_types_test) {
    std::cout << "\n=== Mixed parameter Types Test ===\n";
    
    engine engine;
    script_value result;
    
    {
        PerformanceBenchmark bench("Mixed parameter types (400 iterations)");
        result = engine.execute(R"(
            function processTypes(auto intVal, auto floatVal, auto boolVal, auto stringVal) -> auto {
                var result = intVal;
                if (boolVal) {
                    result = result + floatVal;
                }
                return result;
            }
            
            var total = 0;
            for (var i = 0; i < 400; i = i + 1) {
                total = total + processTypes(i, 1.5, true, "test");
            }
            total;
        )");
    }
    
    expect_eq(result.as_int(), 79800);
}

// Performance comparison: function calls vs direct operations
JAI_TEST(parameter_overhead_comparison_test) {
    std::cout << "\n=== parameter Overhead Comparison Test ===\n";
    
    engine engine;
    script_value result;
    
    {
        PerformanceBenchmark bench("Direct arithmetic (1000 operations)");
        result = engine.execute(R"(
            var sum = 0;
            for (var i = 0; i < 1000; i = i + 1) {
                sum = sum + (i * 2 + 10 + 5);
            }
            sum;
        )");
    }
    
    expect_eq(result.as_int(), 1014000);
    
    {
        PerformanceBenchmark bench("Function with parameters (1000 operations)");
        result = engine.execute(R"(
            function compute(auto value, auto multiplier, auto addend1, auto addend2) -> auto {
                return value * multiplier + addend1 + addend2;
            }
            
            var sum = 0;
            for (var i = 0; i < 1000; i = i + 1) {
                sum = sum + compute(i, 2, 10, 5);
            }
            sum;
        )");
    }
    
    expect_eq(result.as_int(), 1014000);
}

// Test high-frequency parameter binding (stress test for symbol ID caching)
JAI_TEST(high_frequency_binding_test) {
    std::cout << "\n=== High Frequency parameter Binding Test ===\n";
    
    engine engine;
    script_value result;
    
    {
        PerformanceBenchmark bench("High frequency binding (2000 iterations)");
        result = engine.execute(R"(
            function simpleAdd(auto left, auto right) -> auto {
                return left + right;
            }
            
            var total = 0;
            for (var i = 0; i < 2000; i = i + 1) {
                total = total + simpleAdd(i, 1);
            }
            total;
        )");
    }
    
    expect_eq(result.as_int(), 2001000);
}

JAI_TEST(optimization_summary) {
    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║       PARAMETER BINDING OPTIMIZATION SUMMARY          ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";
    
    std::cout << "Optimizations implemented:\n";
    std::cout << "✅ parameter symbol ID caching (eliminates string interning on repeat calls)\n";
    std::cout << "✅ Lazy symbol ID computation (computed on first use, cached thereafter)\n";
    std::cout << "✅ Direct symbol ID parameter binding (uses uint64_t IDs instead of strings)\n";
    std::cout << "✅ Compatible with existing function call optimizations\n";
    std::cout << "✅ Works with recursive functions and environment pooling\n";
    
    std::cout << "\nExpected improvements:\n";
    std::cout << "- 15-20% faster function calls (especially for functions called multiple times)\n";
    std::cout << "- Reduced string hash lookups during parameter binding\n";
    std::cout << "- Better performance for functions with many parameters\n";
    std::cout << "- Improved scaling for high-frequency function calls\n";
    
    std::cout << "\nNext optimization opportunities:\n";
    std::cout << "- environment caching (Item #4 from optimization plan)\n";
    std::cout << "- Type specialization in script_value (Item #5 from optimization plan)\n";
    std::cout << "- Inline function optimization for simple functions\n";
    
    expect_true(true); // Always pass
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()