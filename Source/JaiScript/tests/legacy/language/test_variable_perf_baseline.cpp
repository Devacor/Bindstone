#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <fstream>

using namespace jai;
using namespace jai::test;
using namespace std::chrono;

JAI_TEST_SUITE(VariablePerformanceBaseline)

// Helper to measure execution time
template<typename F>
double measureTime(F&& func, int iterations = 1) {
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        func();
    }
    auto end = high_resolution_clock::now();
    return duration_cast<microseconds>(end - start).count() / static_cast<double>(iterations);
}

JAI_TEST(baseline_measurements) {
    std::cout << "\n=== BASELINE Variable Access Performance ===\n";
    std::ofstream results("variable_perf_baseline.txt");
    
    engine engine;
    
    // Test 1: Simple variable read
    std::cout << "\n1. Simple Variable Read (x1000):\n";
    results << "1. Simple Variable Read (x1000):\n";
    {
        engine.execute("var testVar = 42;");
        
        double time = measureTime([&]() {
            engine.execute(R"(
                var sum = 0;
                for (var i = 0; i < 1000; i = i + 1) {
                    sum = sum + testVar;
                }
                sum;
            )");
        }, 100);
        
        std::cout << "   Time: " << std::fixed << std::setprecision(2) << time << " μs\n";
        results << "   Time: " << time << " μs\n";
    }
    
    // Test 2: Variable write
    std::cout << "\n2. Variable Write (x1000):\n";
    results << "\n2. Variable Write (x1000):\n";
    {
        double time = measureTime([&]() {
            engine.execute(R"(
                var counter = 0;
                for (var i = 0; i < 1000; i = i + 1) {
                    counter = i;
                }
                counter;
            )");
        }, 100);
        
        std::cout << "   Time: " << std::fixed << std::setprecision(2) << time << " μs\n";
        results << "   Time: " << time << " μs\n";
    }
    
    // Test 3: Multiple variable access
    std::cout << "\n3. Multiple Variables (10 vars x 100 accesses each):\n";
    results << "\n3. Multiple Variables (10 vars x 100 accesses each):\n";
    {
        engine.execute(R"(
            var v0 = 0; var v1 = 1; var v2 = 2; var v3 = 3; var v4 = 4;
            var v5 = 5; var v6 = 6; var v7 = 7; var v8 = 8; var v9 = 9;
        )");
        
        double time = measureTime([&]() {
            engine.execute(R"(
                var sum = 0;
                for (var i = 0; i < 100; i = i + 1) {
                    sum = sum + v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9;
                }
                sum;
            )");
        }, 100);
        
        std::cout << "   Time: " << std::fixed << std::setprecision(2) << time << " μs\n";
        results << "   Time: " << time << " μs\n";
    }
    
    // Test 4: Nested scope (simple)
    std::cout << "\n4. Nested Scope Access (parent variable x500):\n";
    results << "\n4. Nested Scope Access (parent variable x500):\n";
    {
        double time = measureTime([&]() {
            engine.execute(R"(
                var outerVar = 100;
                var innerFunc = []() {
                    var sum = 0;
                    for (var i = 0; i < 500; i = i + 1) {
                        sum = sum + outerVar;
                    }
                    return sum;
                };
                innerFunc();
            )");
        }, 100);
        
        std::cout << "   Time: " << std::fixed << std::setprecision(2) << time << " μs\n";
        results << "   Time: " << time << " μs\n";
    }
    
    // Test 5: Deep nesting (3 levels)
    std::cout << "\n5. Deep Nesting (3 levels, x100 accesses):\n";
    results << "\n5. Deep Nesting (3 levels, x100 accesses):\n";
    {
        double time = measureTime([&]() {
            engine.execute(R"(
                var level1 = 1;
                var func1 = []() {
                    var level2 = 2;
                    var func2 = []() {
                        var level3 = 3;
                        var sum = 0;
                        for (var i = 0; i < 100; i = i + 1) {
                            sum = sum + level1 + level2 + level3;
                        }
                        return sum;
                    };
                    return func2();
                };
                func1();
            )");
        }, 100);
        
        std::cout << "   Time: " << std::fixed << std::setprecision(2) << time << " μs\n";
        results << "   Time: " << time << " μs\n";
    }
    
    // Test 6: script_string variable comparison (for string interning baseline)
    std::cout << "\n6. script_string Variable Access (x500):\n";
    results << "\n6. script_string Variable Access (x500):\n";
    {
        engine.execute("var name = \"test_variable_name\";");
        
        double time = measureTime([&]() {
            engine.execute(R"(
                var count = 0;
                for (var i = 0; i < 500; i = i + 1) {
                    if (name == "test_variable_name") {
                        count = count + 1;
                    }
                }
                count;
            )");
        }, 100);
        
        std::cout << "   Time: " << std::fixed << std::setprecision(2) << time << " μs\n";
        results << "   Time: " << time << " μs\n";
    }
    
    results.close();
    std::cout << "\nResults saved to variable_perf_baseline.txt\n";
    
    expect_true(true);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()