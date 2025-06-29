#include "jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <chrono>
#include <iostream>
#include <iomanip>

using namespace jai;
using namespace jai::test;
using namespace std::chrono;

JAI_TEST_SUITE(SpaceshipPerformance)

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

JAI_TEST(spaceship_performance_test) {
    std::cout << "\n=== Spaceship Operator Performance ===" << std::endl;
    
    engine engine;
    const int iterations = 50000;  // High iteration count for spaceship
    
    // Prepare variables
    engine.execute("var a = 42; var b = 17; var c = 42;");
    
    std::cout << "Testing optimized spaceship operator dispatch table..." << std::endl;
    
    // Test spaceship operator (our optimization target)
    double spaceship_time = benchmark_operation("Spaceship (a <=> b)", iterations, [&]() {
        engine.execute("a <=> b;");
    });
    
    // Test spaceship equal case
    double spaceship_equal_time = benchmark_operation("Spaceship equal (a <=> c)", iterations, [&]() {
        engine.execute("a <=> c;");
    });
    
    // Compare with basic arithmetic for baseline
    double addition_time = benchmark_operation("Addition (a + b)", iterations, [&]() {
        engine.execute("a + b;");
    });
    
    double comparison_time = benchmark_operation("Less than (a < b)", iterations, [&]() {
        engine.execute("a < b;");
    });
    
    std::cout << "\n=== Performance Ratios ===" << std::endl;
    std::cout << "Spaceship vs Addition: " << std::fixed << std::setprecision(2) 
              << (spaceship_time / addition_time) << "x" << std::endl;
    std::cout << "Spaceship vs Less Than: " << std::fixed << std::setprecision(2) 
              << (spaceship_time / comparison_time) << "x" << std::endl;
    
    // Our optimization should make spaceship competitive with other operators
    expect_true(spaceship_time < 10.0); // Should be under 10μs per operation
    expect_true(spaceship_time / addition_time < 3.0); // Should be less than 3x slower than addition
    
    std::cout << "\n✅ Spaceship operator optimization working!" << std::endl;
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()