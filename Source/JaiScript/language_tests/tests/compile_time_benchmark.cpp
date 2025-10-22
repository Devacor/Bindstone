#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include "../../../External/ChaiScript-6.1.0/include/chaiscript/chaiscript.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>

using namespace std::chrono;

struct CompileResult {
    std::string test_name;
    long jaiscript_us;
    long chaiscript_us;
    double speedup;
};

std::vector<CompileResult> results;

void measure_compile_time(const std::string& name, const std::string& code, int iterations = 10) {
    std::cout << "Testing: " << name << " (" << iterations << " iterations)" << std::endl;
    
    // Measure JaiScript compilation time
    auto jai_start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        engine->execute(code);
    }
    auto jai_end = high_resolution_clock::now();
    long jai_time = duration_cast<microseconds>(jai_end - jai_start).count() / iterations;
    
    // Measure ChaiScript compilation time  
    auto chai_start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        chaiscript::ChaiScript chai;
        chai.eval(code);
    }
    auto chai_end = high_resolution_clock::now();
    long chai_time = duration_cast<microseconds>(chai_end - chai_start).count() / iterations;
    
    double speedup = static_cast<double>(chai_time) / jai_time;
    results.push_back({name, jai_time, chai_time, speedup});
    
    std::cout << "  JaiScript: " << jai_time << " μs" << std::endl;
    std::cout << "  ChaiScript: " << chai_time << " μs" << std::endl;
    std::cout << "  Speedup: " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl << std::endl;
}

int main() {
    std::cout << "=== JaiScript vs ChaiScript: Compilation Time Benchmark ===\n\n";
    
    // Test 1: Simple arithmetic
    measure_compile_time("Simple arithmetic", R"(
        auto result = 2 + 3 * 4 - 1;
        result;
    )");
    
    // Test 2: Variable declarations
    measure_compile_time("Variable declarations", R"(
        auto a = 10;
        auto b = 20.5;
        auto c = "hello";
        auto d = true;
        auto result = a + b;
        result;
    )");
    
    // Test 3: Function definition - JaiScript only
    std::cout << "Testing: Function definition (JaiScript only)" << std::endl;
    auto jai_func_start = high_resolution_clock::now();
    for (int i = 0; i < 10; ++i) {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        engine->execute(R"(
            auto add = [](int x, int y) -> int {
                return x + y;
            };
            auto result = add(5, 3);
            result;
        )");
    }
    auto jai_func_end = high_resolution_clock::now();
    long jai_func_time = duration_cast<microseconds>(jai_func_end - jai_func_start).count() / 10;
    
    // ChaiScript function definition
    auto chai_func_start = high_resolution_clock::now();
    for (int i = 0; i < 10; ++i) {
        chaiscript::ChaiScript chai;
        chai.eval(R"(
            def add(x, y) {
                return x + y;
            }
            var result = add(5, 3);
            result;
        )");
    }
    auto chai_func_end = high_resolution_clock::now();
    long chai_func_time = duration_cast<microseconds>(chai_func_end - chai_func_start).count() / 10;
    
    double func_speedup = static_cast<double>(chai_func_time) / jai_func_time;
    results.push_back({"Function definition", jai_func_time, chai_func_time, func_speedup});
    
    std::cout << "  JaiScript: " << jai_func_time << " μs" << std::endl;
    std::cout << "  ChaiScript: " << chai_func_time << " μs" << std::endl;
    std::cout << "  Speedup: " << std::fixed << std::setprecision(2) << func_speedup << "x" << std::endl << std::endl;
    
    // Test 4: Control flow - separate implementations due to syntax differences
    std::cout << "Testing: Control flow (5 iterations)" << std::endl;
    auto jai_ctrl_start = high_resolution_clock::now();
    for (int i = 0; i < 5; ++i) {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        engine->execute(R"(
            auto factorial = [](int n) -> int {
                if (n <= 1) {
                    return 1;
                } else {
                    return n * factorial(n - 1);
                }
            };
            auto result = factorial(5);
            result;
        )");
    }
    auto jai_ctrl_end = high_resolution_clock::now();
    long jai_ctrl_time = duration_cast<microseconds>(jai_ctrl_end - jai_ctrl_start).count() / 5;
    
    // ChaiScript control flow
    auto chai_ctrl_start = high_resolution_clock::now();
    for (int i = 0; i < 5; ++i) {
        chaiscript::ChaiScript chai;
        chai.eval(R"(
            def factorial(n) {
                if (n <= 1) {
                    return 1;
                } else {
                    return n * factorial(n - 1);
                }
            }
            var result = factorial(5);
            result;
        )");
    }
    auto chai_ctrl_end = high_resolution_clock::now();
    long chai_ctrl_time = duration_cast<microseconds>(chai_ctrl_end - chai_ctrl_start).count() / 5;
    
    double ctrl_speedup = static_cast<double>(chai_ctrl_time) / jai_ctrl_time;
    results.push_back({"Control flow", jai_ctrl_time, chai_ctrl_time, ctrl_speedup});
    
    std::cout << "  JaiScript: " << jai_ctrl_time << " μs" << std::endl;
    std::cout << "  ChaiScript: " << chai_ctrl_time << " μs" << std::endl;
    std::cout << "  Speedup: " << std::fixed << std::setprecision(2) << ctrl_speedup << "x" << std::endl << std::endl;
    
    // Test 5: Array operations
    measure_compile_time("Array operations", R"(
        auto arr = [1, 2, 3, 4, 5];
        auto sum = 0;
        for (auto i = 0; i < arr.size(); i = i + 1) {
            sum = sum + arr[i];
        }
        sum;
    )");
    
    // Test 6: Class definition - JaiScript
    std::cout << "Testing: Class definition (JaiScript only)" << std::endl;
    auto jai_class_start = high_resolution_clock::now();
    for (int i = 0; i < 5; ++i) {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        engine->execute(R"(
            class Point {
                float x = 0.0;
                float y = 0.0;
                
                Point(float px, float py) {
                    x = px;
                    y = py;
                }
                
                float distance_to(Point other) {
                    auto dx = x - other.x;
                    auto dy = y - other.y;
                    return sqrt(dx * dx + dy * dy);
                }
            }
            
            auto p1 = Point(0.0, 0.0);
            auto p2 = Point(3.0, 4.0);
            auto dist = p1.distance_to(p2);
            dist;
        )");
    }
    auto jai_class_end = high_resolution_clock::now();
    long jai_class_time = duration_cast<microseconds>(jai_class_end - jai_class_start).count() / 5;
    
    // Test 6: Class definition - ChaiScript
    auto chai_class_start = high_resolution_clock::now();
    for (int i = 0; i < 5; ++i) {
        chaiscript::ChaiScript chai;
        chai.add(chaiscript::fun([](double x) { return std::sqrt(x); }), "sqrt");
        chai.eval(R"(
            class Point {
                var x;
                var y;
                def Point(px, py) {
                    this.x = px;
                    this.y = py;
                }
                def distance_to(other) {
                    var dx = this.x - other.x;
                    var dy = this.y - other.y;
                    return sqrt(dx * dx + dy * dy);
                }
            };
            
            var p1 = Point(0.0, 0.0);
            var p2 = Point(3.0, 4.0);
            var dist = p1.distance_to(p2);
            dist;
        )");
    }
    auto chai_class_end = high_resolution_clock::now();
    long chai_class_time = duration_cast<microseconds>(chai_class_end - chai_class_start).count() / 5;
    
    double class_speedup = static_cast<double>(chai_class_time) / jai_class_time;
    results.push_back({"Class definition", jai_class_time, chai_class_time, class_speedup});
    
    std::cout << "  JaiScript: " << jai_class_time << " μs" << std::endl;
    std::cout << "  ChaiScript: " << chai_class_time << " μs" << std::endl;
    std::cout << "  Speedup: " << std::fixed << std::setprecision(2) << class_speedup << "x" << std::endl << std::endl;
    
    // Test 7: Complex expression
    measure_compile_time("Complex expression", R"(
        auto complex_calc = [](float a, float b, float c) -> float {
            return (a * b + c) / (a - b) * (c + a) - b * c / a;
        };
        auto result = complex_calc(10.5, 3.2, 7.8);
        result;
    )");
    
    // Summary
    std::cout << "\n=== Compilation Time Summary ===\n";
    std::cout << std::left;
    std::cout << std::setw(25) << "Test" 
              << std::setw(15) << "JaiScript (μs)" 
              << std::setw(15) << "ChaiScript (μs)" 
              << std::setw(10) << "Speedup" << std::endl;
    std::cout << std::string(70, '-') << std::endl;
    
    double total_speedup = 0;
    for (const auto& result : results) {
        std::cout << std::setw(25) << result.test_name
                  << std::setw(15) << result.jaiscript_us
                  << std::setw(15) << result.chaiscript_us
                  << std::setw(10) << (std::to_string(result.speedup).substr(0, 4) + "x") << std::endl;
        total_speedup += result.speedup;
    }
    
    std::cout << std::string(70, '-') << std::endl;
    std::cout << "Average speedup: " << std::fixed << std::setprecision(2) 
              << (total_speedup / results.size()) << "x" << std::endl;
    
    if (total_speedup / results.size() > 1.0) {
        std::cout << "JaiScript compiles faster on average!" << std::endl;
    } else {
        std::cout << "ChaiScript compiles faster on average!" << std::endl;
    }
    
    return 0;
}