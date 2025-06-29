#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <chrono>
#include <iostream>
#include <iomanip>

using namespace jai;
using namespace jai::test;
using namespace std::chrono;

JAI_TEST_SUITE(VariableAccessPerformance)

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

JAI_TEST(simple_variable_access) {
    std::cout << "\n=== Simple Variable Access ===\n";
    
    engine engine;
    
    // Setup: Create many variables
    engine.execute(R"(
        var a = 1; var b = 2; var c = 3; var d = 4; var e = 5;
        var f = 6; var g = 7; var h = 8; var i = 9; var j = 10;
    )");
    
    // Test: Access variables many times
    double time = measureTime([&]() {
        engine.execute(R"(
            var sum = 0;
            for (var x = 0; x < 100; x = x + 1) {
                sum = sum + a + b + c + d + e + f + g + h + i + j;
            }
            sum;
        )");
    }, 100);
    
    std::cout << "Simple variable access (1000 lookups): " 
              << std::fixed << std::setprecision(2) << time << " μs\n";
    
    expect_true(time > 0);
}

JAI_TEST(nested_scope_access) {
    std::cout << "\n=== Nested Scope Access ===\n";
    
    engine engine;
    
    // Test: Deep nesting with parent scope access
    double time = measureTime([&]() {
        engine.execute(R"(
            var level0 = 100;
            auto f1 = []() {
                var level1 = 200;
                auto f2 = []() {
                    var level2 = 300;
                    auto f3 = []() {
                        var level3 = 400;
                        auto f4 = []() {
                            var level4 = 500;
                            auto f5 = []() {
                                var sum = 0;
                                for (var i = 0; i < 50; i = i + 1) {
                                    sum = sum + level0 + level1 + level2 + level3 + level4;
                                }
                                return sum;
                            };
                            return f5();
                        };
                        return f4();
                    };
                    return f3();
                };
                return f2();
            };
            f1();
        )");
    }, 50);
    
    std::cout << "Nested scope access (5 levels, 250 parent lookups): " 
              << std::fixed << std::setprecision(2) << time << " μs\n";
    
    expect_true(time > 0);
}

JAI_TEST(repeated_identifier_access) {
    std::cout << "\n=== Repeated identifier Access ===\n";
    
    engine engine;
    
    // Test: Same variable accessed many times (should benefit most from caching)
    double time = measureTime([&]() {
        engine.execute(R"(
            var counter = 0;
            for (var i = 0; i < 200; i = i + 1) {
                counter = counter + 1;
                counter = counter + 1;
                counter = counter + 1;
                counter = counter + 1;
                counter = counter + 1;
            }
            counter;
        )");
    }, 100);
    
    std::cout << "Repeated identifier access (1000 accesses): " 
              << std::fixed << std::setprecision(2) << time << " μs\n";
    
    expect_true(time > 0);
}

JAI_TEST(mixed_local_parent_access) {
    std::cout << "\n=== Mixed Local/Parent Access ===\n";
    
    engine engine;
    
    // Test: Mix of local and parent scope variables
    double time = measureTime([&]() {
        engine.execute(R"(
            var global1 = 10;
            var global2 = 20;
            var global3 = 30;
            
            auto compute = [](int param) {
                var local1 = param * 2;
                var local2 = param * 3;
                var sum = 0;
                
                for (var i = 0; i < 100; i = i + 1) {
                    sum = sum + global1 + local1 + global2 + local2 + global3;
                }
                return sum;
            };
            
            var total = 0;
            for (var j = 0; j < 10; j = j + 1) {
                total = total + compute(j);
            }
            total;
        )");
    }, 50);
    
    std::cout << "Mixed local/parent access (5000 lookups): " 
              << std::fixed << std::setprecision(2) << time << " μs\n";
    
    expect_true(time > 0);
}

JAI_TEST(closure_variable_capture) {
    std::cout << "\n=== Closure Variable capture ===\n";
    
    engine engine;
    
    // Test: Closures capturing variables
    double time = measureTime([&]() {
        engine.execute(R"(
            var makeCounter = []() {
                var count = 0;
                return [&count]() {
                    count = count + 1;
                    return count;
                };
            };
            
            var c1 = makeCounter();
            var c2 = makeCounter();
            
            var sum = 0;
            for (var i = 0; i < 100; i = i + 1) {
                sum = sum + c1() + c2();
            }
            sum;
        )");
    }, 50);
    
    std::cout << "Closure variable capture (200 captured var accesses): " 
              << std::fixed << std::setprecision(2) << time << " μs\n";
    
    expect_true(time > 0);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()