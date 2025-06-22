#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <chrono>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(PerformanceTests)

// Test engine creation time
JAI_TEST(engine_creation_time) {
    auto start = std::chrono::high_resolution_clock::now();
    Engine engine;
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Engine creation: " << duration.count() << " μs" << std::endl;
    
    // Engine should be created in reasonable time (< 1ms for this test)
    expect_true(duration.count() < 1000, "Engine creation took too long");
}

// Test simple arithmetic performance
JAI_TEST(simple_arithmetic_performance) {
    Engine engine;
    
    auto start = std::chrono::high_resolution_clock::now();
    auto result = engine.execute("1 + 2 * 3;");
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Simple arithmetic: " << duration.count() << " μs" << std::endl;
    
    expect_eq(result.as<int>(), 7);
    expect_true(duration.count() < 500, "Arithmetic too slow");
}

// Test variable assignment performance
JAI_TEST(variable_assignment_performance) {
    Engine engine;
    
    auto start = std::chrono::high_resolution_clock::now();
    auto result = engine.execute("var x = 42; x = x + 1; x;");
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Variable assignment: " << duration.count() << " μs" << std::endl;
    
    expect_eq(result.as<int>(), 43);
    expect_true(duration.count() < 100, "Variable assignment too slow");
}

// Test function definition and call performance
JAI_TEST(function_definition_performance) {
    Engine engine;
    
    auto start = std::chrono::high_resolution_clock::now();
    auto result = engine.execute("auto add(auto a, auto b) -> auto { return a + b; } add(5, 3);");
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Function definition: " << duration.count() << " μs" << std::endl;
    
    expect_eq(result.as<int>(), 8);
    expect_true(duration.count() < 200, "Function definition too slow");
}

// Test loop performance (100 iterations)
JAI_TEST(loop_performance_100) {
    Engine engine;
    
    auto start = std::chrono::high_resolution_clock::now();
    auto result = engine.execute(R"(
        var sum = 0;
        for (var i = 0; i < 100; i = i + 1) {
            sum = sum + i;
        }
        sum;
    )");
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Loop (100 iterations): " << duration.count() << " μs" << std::endl;
    
    expect_eq(result.as<int>(), 4950); // Sum of 0..99
    expect_true(duration.count() < 1000, "Loop too slow");
}

// Test nested function with loop
JAI_TEST(nested_function_with_loop_performance) {
    Engine engine;
    
    auto start = std::chrono::high_resolution_clock::now();
    auto result = engine.execute(R"(
        auto sum(auto n) -> auto {
            var total = 0;
            for (var i = 0; i < n; i = i + 1) {
                total = total + i;
            }
            return total;
        }
        sum(50);
    )");
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Nested function with loop: " << duration.count() << " μs" << std::endl;
    
    expect_eq(result.as<int>(), 1225); // Sum of 0..49
    expect_true(duration.count() < 500, "Nested function too slow");
}

// Benchmark simple arithmetic operations
JAI_BENCHMARK(simple_arithmetic_benchmark) {
    static Engine engine;
    engine.execute("1 + 2 * 3;");
}

// Benchmark variable operations
JAI_BENCHMARK(variable_operations_benchmark) {
    static Engine engine;
    engine.execute("var x = 42; x = x + 1; x;");
}

// Benchmark function calls
JAI_BENCHMARK(function_calls_benchmark) {
    static Engine engine;
    static bool setup = false;
    if (!setup) {
        engine.execute("auto add(auto a, auto b) -> auto { return a + b; }");
        setup = true;
    }
    engine.execute("add(5, 3);");
}

// Benchmark small loops
JAI_BENCHMARK(small_loop_benchmark) {
    static Engine engine;
    engine.execute(R"(
        var sum = 0;
        for (var i = 0; i < 10; i = i + 1) {
            sum = sum + i;
        }
        sum;
    )");
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()