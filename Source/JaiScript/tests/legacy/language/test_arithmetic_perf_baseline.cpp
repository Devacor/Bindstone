#include "../jai_test.hpp"
#include "../../include/jaiscript/jaiscript.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(ArithmeticPerformanceBaseline)

JAI_TEST(arithmetic_baseline_measurements) {
    std::cout << "\n=== BASELINE Arithmetic Performance ===\n";
    
    engine engine;
    
    // Test 1: Pure integer arithmetic (x1000)
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        engine.execute(R"(
            var sum = 0;
            for (var i = 0; i < 1000; i = i + 1) {
                sum = sum + i * 2 - i / 2 + i % 3;
            }
            sum
        )");
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "1. Pure Integer Arithmetic (x1000):\n";
        std::cout << "   Time: " << std::fixed << std::setprecision(2) << duration << " μs\n\n";
    }
    
    // Test 2: Floating point arithmetic (x1000)
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        engine.execute(R"(
            var sum = 0.0;
            for (var i = 0; i < 1000; i = i + 1) {
                sum = sum + i * 1.5 - i / 2.5 + i * 0.7;
            }
            sum
        )");
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "2. Floating Point Arithmetic (x1000):\n";
        std::cout << "   Time: " << std::fixed << std::setprecision(2) << duration << " μs\n\n";
    }
    
    // Test 3: Mixed type arithmetic (x1000)
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        engine.execute(R"(
            var sum = 0;
            for (var i = 0; i < 1000; i = i + 1) {
                sum = sum + i * 1.5 - i / 2 + i % 3;
            }
            sum
        )");
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "3. Mixed Type Arithmetic (x1000):\n";
        std::cout << "   Time: " << std::fixed << std::setprecision(2) << duration << " μs\n\n";
    }
    
    // Test 4: Complex expression evaluation (x100)
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        engine.execute(R"(
            var result = 0;
            for (var i = 1; i < 101; i = i + 1) {
                result = result + ((i * 2 + 3) * (i - 1) / 2 - i % 5 + i * i / 10);
            }
            result
        )");
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "4. Complex expression (x100):\n";
        std::cout << "   Time: " << std::fixed << std::setprecision(2) << duration << " μs\n\n";
    }
    
    // Test 5: Prime number calculation (compute-intensive)
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        engine.execute(R"(
            auto isPrime(auto n) -> auto {
                if (n < 2) { return 0; }
                var i = 2;
                while (i * i <= n) {
                    if (n % i == 0) { return 0; }
                    i = i + 1;
                }
                return 1;
            }
            
            var count = 0;
            for (var i = 2; i < 200; i = i + 1) {
                if (isPrime(i)) { count = count + 1; }
            }
            count
        )");
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "5. Prime Numbers (up to 200):\n";
        std::cout << "   Time: " << std::fixed << std::setprecision(2) << duration << " μs\n\n";
    }
    
    // Test 6: Fibonacci calculation (recursive)
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        auto result = engine.eval(R"(
            function fib(auto n) -> auto {
                if (n <= 1) { return n; }
                return fib(n - 1) + fib(n - 2);
            }
            fib(15)
        )");
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "6. Fibonacci(15) recursive:\n";
        std::cout << "   Time: " << std::fixed << std::setprecision(2) << duration << " μs\n";
        std::cout << "   Result: " << result.as_int() << "\n";
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()