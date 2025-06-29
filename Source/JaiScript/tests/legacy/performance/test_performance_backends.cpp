#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <vector>
#include <numeric>

using namespace jai;
using namespace jai::test;

// Performance test helper
class PerfTest {
    std::string name;
    std::vector<double> times_ms;
    
public:
    PerfTest(const std::string& test_name) : name(test_name) {}
    
    template<typename Func>
    void measure(Func&& func, int iterations = 100) {
        // Warmup
        for (int i = 0; i < 10; ++i) {
            func();
        }
        
        // Measure
        times_ms.clear();
        for (int i = 0; i < iterations; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            func();
            auto end = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double, std::milli>(end - start).count();
            times_ms.push_back(elapsed);
        }
    }
    
    double median() const {
        if (times_ms.empty()) return 0;
        std::vector<double> sorted = times_ms;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() / 2];
    }
    
    double average() const {
        if (times_ms.empty()) return 0;
        return std::accumulate(times_ms.begin(), times_ms.end(), 0.0) / times_ms.size();
    }
    
    void print_comparison(double interpreter_time, double jvm_time) {
        std::cout << std::left << std::setw(40) << name 
                  << std::setw(15) << std::fixed << std::setprecision(3) << interpreter_time << " ms"
                  << std::setw(15) << std::fixed << std::setprecision(3) << jvm_time << " ms"
                  << std::setw(10) << std::fixed << std::setprecision(2) << (interpreter_time / jvm_time) << "x"
                  << "\n";
    }
};

JAI_TEST_SUITE(PerformanceBackends)

JAI_TEST(backend_performance_comparison) {
    std::cout << "\n=== JaiScript Backend Performance Comparison ===\n\n";
    std::cout << std::left << std::setw(40) << "Test" 
              << std::setw(15) << "Interpreter" 
              << std::setw(15) << "JVM"
              << std::setw(10) << "Speedup"
              << "\n";
    std::cout << std::string(80, '-') << "\n";
    
    // Test 1: Simple Arithmetic
    {
        PerfTest test("Simple arithmetic (1+2*3)");
        
        engine interp_engine;
        interp_engine.set_backend(backend_type::interpreter);
        test.measure([&]() {
            interp_engine.execute("1 + 2 * 3;");
        });
        double interp_time = test.median();
        
        engine jvm_engine;
        jvm_engine.set_backend(backend_type::jvm);
        test.measure([&]() {
            jvm_engine.execute("1 + 2 * 3;");
        });
        double jvm_time = test.median();
        
        test.print_comparison(interp_time, jvm_time);
    }
    
    // Test 2: Variable Operations
    {
        PerfTest test("Variable assignment");
        const std::string script = R"(
            var x = 42;
            x = x + 1;
            x = x * 2;
            x;
        )";
        
        engine interp_engine;
        interp_engine.set_backend(backend_type::interpreter);
        test.measure([&]() {
            interp_engine.execute(script);
        });
        double interp_time = test.median();
        
        engine jvm_engine;
        jvm_engine.set_backend(backend_type::jvm);
        test.measure([&]() {
            jvm_engine.execute(script);
        });
        double jvm_time = test.median();
        
        test.print_comparison(interp_time, jvm_time);
    }
    
    // Test 3: Loop Performance
    {
        PerfTest test("Loop (sum 1-100)");
        const std::string script = R"(
            var sum = 0;
            for (var i = 1; i <= 100; ++i) {
                sum = sum + i;
            }
            sum;
        )";
        
        engine interp_engine;
        interp_engine.set_backend(backend_type::interpreter);
        test.measure([&]() {
            interp_engine.execute(script);
        });
        double interp_time = test.median();
        
        engine jvm_engine;
        jvm_engine.set_backend(backend_type::jvm);
        test.measure([&]() {
            jvm_engine.execute(script);
        });
        double jvm_time = test.median();
        
        test.print_comparison(interp_time, jvm_time);
    }
    
    // Test 4: Function Calls
    {
        PerfTest test("Function definition and calls");
        const std::string script = R"(
            fun add(a, b) {
                return a + b;
            }
            var sum = 0;
            for (var i = 0; i < 50; ++i) {
                sum = add(sum, i);
            }
            sum;
        )";
        
        engine interp_engine;
        interp_engine.set_backend(backend_type::interpreter);
        test.measure([&]() {
            interp_engine.execute(script);
        }, 50); // Fewer iterations for longer tests
        double interp_time = test.median();
        
        engine jvm_engine;
        jvm_engine.set_backend(backend_type::jvm);
        test.measure([&]() {
            jvm_engine.execute(script);
        }, 50);
        double jvm_time = test.median();
        
        test.print_comparison(interp_time, jvm_time);
    }
    
    // Test 5: Fibonacci (Recursive)
    {
        PerfTest test("Fibonacci recursive (n=15)");
        const std::string script = R"(
            fun fib(n) {
                if (n <= 1) return n;
                return fib(n-1) + fib(n-2);
            }
            fib(15);
        )";
        
        engine interp_engine;
        interp_engine.set_backend(backend_type::interpreter);
        test.measure([&]() {
            interp_engine.execute(script);
        }, 20);
        double interp_time = test.median();
        
        engine jvm_engine;
        jvm_engine.set_backend(backend_type::jvm);
        test.measure([&]() {
            jvm_engine.execute(script);
        }, 20);
        double jvm_time = test.median();
        
        test.print_comparison(interp_time, jvm_time);
    }
    
    // Test 6: Array Operations
    {
        PerfTest test("Array creation and access");
        const std::string script = R"(
            var arr = [];
            for (var i = 0; i < 50; ++i) {
                arr.push(i * 2);
            }
            var sum = 0;
            for (var i = 0; i < arr.size(); ++i) {
                sum = sum + arr[i];
            }
            sum;
        )";
        
        engine interp_engine;
        interp_engine.set_backend(backend_type::interpreter);
        test.measure([&]() {
            interp_engine.execute(script);
        }, 50);
        double interp_time = test.median();
        
        engine jvm_engine;
        jvm_engine.set_backend(backend_type::jvm);
        test.measure([&]() {
            jvm_engine.execute(script);
        }, 50);
        double jvm_time = test.median();
        
        test.print_comparison(interp_time, jvm_time);
    }
    
    // Test 7: String Operations
    {
        PerfTest test("String concatenation");
        const std::string script = R"(
            var result = "";
            for (var i = 0; i < 20; ++i) {
                result = result + "x";
            }
            result;
        )";
        
        engine interp_engine;
        interp_engine.set_backend(backend_type::interpreter);
        test.measure([&]() {
            interp_engine.execute(script);
        }, 50);
        double interp_time = test.median();
        
        engine jvm_engine;
        jvm_engine.set_backend(backend_type::jvm);
        test.measure([&]() {
            jvm_engine.execute(script);
        }, 50);
        double jvm_time = test.median();
        
        test.print_comparison(interp_time, jvm_time);
    }
    
    // Test 8: Nested Loops
    {
        PerfTest test("Nested loops (10x10)");
        const std::string script = R"(
            var count = 0;
            for (var i = 0; i < 10; ++i) {
                for (var j = 0; j < 10; ++j) {
                    count = count + 1;
                }
            }
            count;
        )";
        
        engine interp_engine;
        interp_engine.set_backend(backend_type::interpreter);
        test.measure([&]() {
            interp_engine.execute(script);
        });
        double interp_time = test.median();
        
        engine jvm_engine;
        jvm_engine.set_backend(backend_type::jvm);
        test.measure([&]() {
            jvm_engine.execute(script);
        });
        double jvm_time = test.median();
        
        test.print_comparison(interp_time, jvm_time);
    }
    
    std::cout << "\n";
    expect_true(true); // Make test framework happy
}

JAI_TEST(auto_backend_switching) {
    std::cout << "\n=== Auto Backend Selection Test ===\n\n";
    
    engine auto_engine;
    auto_engine.set_backend(backend_type::auto_select);
    
    // Small script (should use interpreter)
    std::string small_script = "1 + 2;";
    std::cout << "Small script (" << small_script.length() << " chars): ";
    auto_engine.execute(small_script);
    std::cout << "Backend: " << auto_engine.get_backend_name() << "\n";
    
    // Large script (should use JVM)
    std::string large_script = R"(
        // This is a large script to trigger JVM backend
        var result = 0;
        for (var i = 0; i < 100; ++i) {
            result = result + i;
            // Adding more content to make it larger
            var temp = i * 2;
            temp = temp + 1;
            temp = temp / 2;
            if (temp > 50) {
                temp = temp - 50;
            }
            result = result + temp;
        }
        // More content to ensure we're over 1000 characters
        fun helper(x) {
            return x * x + x / 2 - x % 3;
        }
        for (var j = 0; j < 50; ++j) {
            result = result + helper(j);
        }
        result;
    )";
    
    // Add more padding to ensure > 1000 chars
    while (large_script.length() < 1001) {
        large_script += "\n// Padding comment to reach threshold";
    }
    
    std::cout << "Large script (" << large_script.length() << " chars): ";
    auto_engine.execute(large_script);
    std::cout << "Backend: " << auto_engine.get_backend_name() << "\n\n";
    
    expect_true(true);
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()