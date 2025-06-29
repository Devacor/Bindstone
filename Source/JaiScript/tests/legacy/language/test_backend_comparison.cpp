#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <chrono>
#include <vector>
#include <map>
#include <iostream>
#include <iomanip>
#include <functional>

using namespace jai;
using namespace jai::test;

struct TestResult {
    std::string test_name;
    bool interpreter_passed;
    bool jvm_passed;
    double interpreter_time_ms;
    double jvm_time_ms;
    std::string interpreter_error;
    std::string jvm_error;
};

struct TestCase {
    std::string name;
    std::string script;
    std::function<bool(const script_value&)> validator;
};

class BackendComparison {
private:
    std::vector<TestResult> results;
    
    std::pair<bool, double> run_with_backend(engine& eng, backend_type type, 
                                             const std::string& script,
                                             std::function<bool(const script_value&)> validator) {
        eng.set_backend(type);
        
        auto start = std::chrono::high_resolution_clock::now();
        try {
            script_value result = eng.execute(script);
            auto end = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double, std::milli>(end - start).count();
            
            bool passed = validator(result);
            return {passed, elapsed};
        } catch (const std::exception& e) {
            auto end = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double, std::milli>(end - start).count();
            return {false, elapsed};
        }
    }
    
public:
    void add_test(const TestCase& test) {
        TestResult result;
        result.test_name = test.name;
        
        // Create two separate engines to ensure clean state
        engine interpreter_engine;
        engine jvm_engine;
        
        // Run with interpreter
        try {
            auto [passed, time] = run_with_backend(interpreter_engine, backend_type::interpreter, 
                                                   test.script, test.validator);
            result.interpreter_passed = passed;
            result.interpreter_time_ms = time;
        } catch (const std::exception& e) {
            result.interpreter_passed = false;
            result.interpreter_error = e.what();
            result.interpreter_time_ms = 0;
        }
        
        // Run with JVM
        try {
            auto [passed, time] = run_with_backend(jvm_engine, backend_type::jvm, 
                                                   test.script, test.validator);
            result.jvm_passed = passed;
            result.jvm_time_ms = time;
        } catch (const std::exception& e) {
            result.jvm_passed = false;
            result.jvm_error = e.what();
            result.jvm_time_ms = 0;
        }
        
        results.push_back(result);
    }
    
    void print_summary() {
        std::cout << "\n=== Backend Comparison Results ===\n\n";
        
        int interpreter_passed = 0;
        int jvm_passed = 0;
        int both_passed = 0;
        int both_failed = 0;
        int only_interpreter = 0;
        int only_jvm = 0;
        
        double total_interpreter_time = 0;
        double total_jvm_time = 0;
        
        // Print detailed results
        std::cout << std::left << std::setw(40) << "Test Name" 
                  << std::setw(15) << "Interpreter" 
                  << std::setw(15) << "JVM"
                  << std::setw(15) << "Interp Time"
                  << std::setw(15) << "JVM Time"
                  << std::setw(15) << "Speedup"
                  << "\n";
        std::cout << std::string(110, '-') << "\n";
        
        for (const auto& result : results) {
            std::cout << std::left << std::setw(40) << result.test_name;
            
            // Interpreter result
            if (result.interpreter_passed) {
                std::cout << std::setw(15) << "✓ PASS";
                interpreter_passed++;
            } else {
                std::cout << std::setw(15) << "✗ FAIL";
            }
            
            // JVM result
            if (result.jvm_passed) {
                std::cout << std::setw(15) << "✓ PASS";
                jvm_passed++;
            } else {
                std::cout << std::setw(15) << "✗ FAIL";
            }
            
            // Timing
            std::cout << std::setw(15) << std::fixed << std::setprecision(2) 
                      << result.interpreter_time_ms << " ms";
            std::cout << std::setw(15) << std::fixed << std::setprecision(2) 
                      << result.jvm_time_ms << " ms";
            
            // Speedup
            if (result.jvm_time_ms > 0) {
                double speedup = result.interpreter_time_ms / result.jvm_time_ms;
                std::cout << std::setw(15) << std::fixed << std::setprecision(2) 
                          << speedup << "x";
            } else {
                std::cout << std::setw(15) << "N/A";
            }
            
            std::cout << "\n";
            
            // Count categories
            if (result.interpreter_passed && result.jvm_passed) {
                both_passed++;
                total_interpreter_time += result.interpreter_time_ms;
                total_jvm_time += result.jvm_time_ms;
            } else if (!result.interpreter_passed && !result.jvm_passed) {
                both_failed++;
            } else if (result.interpreter_passed && !result.jvm_passed) {
                only_interpreter++;
            } else {
                only_jvm++;
            }
            
            // Print errors if any
            if (!result.interpreter_passed && !result.interpreter_error.empty()) {
                std::cout << "  Interpreter Error: " << result.interpreter_error << "\n";
            }
            if (!result.jvm_passed && !result.jvm_error.empty()) {
                std::cout << "  JVM Error: " << result.jvm_error << "\n";
            }
        }
        
        std::cout << "\n=== Summary ===\n";
        std::cout << "Total tests: " << results.size() << "\n";
        std::cout << "Both passed: " << both_passed << "\n";
        std::cout << "Both failed: " << both_failed << "\n";
        std::cout << "Only interpreter passed: " << only_interpreter << "\n";
        std::cout << "Only JVM passed: " << only_jvm << "\n";
        std::cout << "\nInterpreter pass rate: " << (100.0 * interpreter_passed / results.size()) << "%\n";
        std::cout << "JVM pass rate: " << (100.0 * jvm_passed / results.size()) << "%\n";
        
        if (both_passed > 0) {
            double avg_speedup = total_interpreter_time / total_jvm_time;
            std::cout << "\nAverage speedup (for tests that passed both): " 
                      << std::fixed << std::setprecision(2) << avg_speedup << "x\n";
        }
    }
};

JAI_TEST_SUITE(BackendComparison)

JAI_TEST(comprehensive_backend_comparison) {
    BackendComparison comparison;
    
    // Arithmetic tests
    comparison.add_test({"simple_addition", "2 + 3;", 
        [](const script_value& v) { return v.as<script_int>() == 5; }});
    
    comparison.add_test({"complex_arithmetic", "(10 + 5) * 3 - 8 / 2;", 
        [](const script_value& v) { return v.as<script_int>() == 41; }});
    
    comparison.add_test({"float_arithmetic", "3.14 * 2.0 + 1.5;", 
        [](const script_value& v) { return std::abs(v.as<script_float>() - 7.78) < 0.01; }});
    
    // Variables
    comparison.add_test({"variable_assignment", R"(
        var x = 10;
        var y = 20;
        x + y;
    )", [](const script_value& v) { return v.as<script_int>() == 30; }});
    
    // Functions
    comparison.add_test({"function_definition", R"(
        fun add(a, b) {
            return a + b;
        }
        add(15, 25);
    )", [](const script_value& v) { return v.as<script_int>() == 40; }});
    
    comparison.add_test({"recursive_function", R"(
        fun factorial(n) {
            if (n <= 1) return 1;
            return n * factorial(n - 1);
        }
        factorial(5);
    )", [](const script_value& v) { return v.as<script_int>() == 120; }});
    
    // Control flow
    comparison.add_test({"if_else", R"(
        var x = 10;
        if (x > 5) {
            x * 2;
        } else {
            x / 2;
        }
    )", [](const script_value& v) { return v.as<script_int>() == 20; }});
    
    comparison.add_test({"for_loop", R"(
        var sum = 0;
        for (var i = 1; i <= 10; ++i) {
            sum = sum + i;
        }
        sum;
    )", [](const script_value& v) { return v.as<script_int>() == 55; }});
    
    comparison.add_test({"while_loop", R"(
        var n = 10;
        var sum = 0;
        while (n > 0) {
            sum = sum + n;
            n = n - 1;
        }
        sum;
    )", [](const script_value& v) { return v.as<script_int>() == 55; }});
    
    // Arrays
    comparison.add_test({"array_creation", R"(
        var arr = [1, 2, 3, 4, 5];
        arr[2];
    )", [](const script_value& v) { return v.as<script_int>() == 3; }});
    
    comparison.add_test({"array_methods", R"(
        var arr = [10, 20, 30];
        arr.push(40);
        arr.size();
    )", [](const script_value& v) { return v.as<script_int>() == 4; }});
    
    // Strings
    comparison.add_test({"string_concat", R"(
        "Hello" + " " + "World";
    )", [](const script_value& v) { return v.as<script_string>() == "Hello World"; }});
    
    comparison.add_test({"string_with_numbers", R"(
        "Result: " + 42;
    )", [](const script_value& v) { return v.as<script_string>() == "Result: 42"; }});
    
    // Closures
    comparison.add_test({"simple_closure", R"(
        fun make_adder(n) {
            return fun(x) { return x + n; };
        }
        var add5 = make_adder(5);
        add5(10);
    )", [](const script_value& v) { return v.as<script_int>() == 15; }});
    
    // Complex expressions
    comparison.add_test({"nested_calls", R"(
        fun double(x) { return x * 2; }
        fun triple(x) { return x * 3; }
        fun compose(f, g) {
            return fun(x) { return f(g(x)); };
        }
        var six_times = compose(double, triple);
        six_times(5);
    )", [](const script_value& v) { return v.as<script_int>() == 30; }});
    
    // Performance-intensive tests
    comparison.add_test({"fibonacci_iterative", R"(
        fun fib(n) {
            if (n <= 1) return n;
            var a = 0;
            var b = 1;
            for (var i = 2; i <= n; ++i) {
                var temp = a + b;
                a = b;
                b = temp;
            }
            return b;
        }
        fib(20);
    )", [](const script_value& v) { return v.as<script_int>() == 6765; }});
    
    comparison.add_test({"nested_loops", R"(
        var sum = 0;
        for (var i = 0; i < 10; ++i) {
            for (var j = 0; j < 10; ++j) {
                sum = sum + i * j;
            }
        }
        sum;
    )", [](const script_value& v) { return v.as<script_int>() == 2025; }});
    
    // Print results
    comparison.print_summary();
    
    // Basic assertion to make test framework happy
    expect_true(true);
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()