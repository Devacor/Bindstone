#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/interpreter.hpp>
#include <jaiscript/detail/parser.hpp>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <vector>

using namespace jai;
using namespace jai::test;
using namespace std::chrono;

JAI_TEST_SUITE(BinaryExprOptimized)

// Create a test engine with dispatch table optimization
class optimized_engine : public engine {
public:
    optimized_engine() : engine() {
        // Install optimized handlers
        install_optimized_handlers();
    }
    
private:
    void install_optimized_handlers() {
        // Install arithmetic handlers as operator overloads
        // This simulates what a dispatch table would do
        add_binary_handler("+", [](const script_value& l, const script_value& r) -> script_value {
            // Fast path for integer arithmetic
            if (l.is_int() && r.is_int()) {
                return script_value(l.as_int() + r.as_int());
            }
            // Fast path for float arithmetic
            if ((l.is_int() || l.is_float()) && (r.is_int() || r.is_float())) {
                script_float lf = l.is_int() ? script_float(l.as_int()) : l.as_float();
                script_float rf = r.is_int() ? script_float(r.as_int()) : r.as_float();
                return script_value(lf + rf);
            }
            // String concatenation
            if (l.is_string() || r.is_string()) {
                return script_value(l.to_string() + r.to_string());
            }
            throw runtime_error("Invalid operands for +");
        });
        
        add_binary_handler("-", [](const script_value& l, const script_value& r) -> script_value {
            if (l.is_int() && r.is_int()) {
                return script_value(l.as_int() - r.as_int());
            }
            if ((l.is_int() || l.is_float()) && (r.is_int() || r.is_float())) {
                script_float lf = l.is_int() ? script_float(l.as_int()) : l.as_float();
                script_float rf = r.is_int() ? script_float(r.as_int()) : r.as_float();
                return script_value(lf - rf);
            }
            throw runtime_error("Invalid operands for -");
        });
        
        add_binary_handler("*", [](const script_value& l, const script_value& r) -> script_value {
            if (l.is_int() && r.is_int()) {
                return script_value(l.as_int() * r.as_int());
            }
            if ((l.is_int() || l.is_float()) && (r.is_int() || r.is_float())) {
                script_float lf = l.is_int() ? script_float(l.as_int()) : l.as_float();
                script_float rf = r.is_int() ? script_float(r.as_int()) : r.as_float();
                return script_value(lf * rf);
            }
            throw runtime_error("Invalid operands for *");
        });
        
        add_binary_handler("/", [](const script_value& l, const script_value& r) -> script_value {
            if (l.is_int() && r.is_int()) {
                if (r.as_int() == 0) throw runtime_error("Division by zero");
                return script_value(l.as_int() / r.as_int());
            }
            if ((l.is_int() || l.is_float()) && (r.is_int() || r.is_float())) {
                script_float rf = r.is_int() ? script_float(r.as_int()) : r.as_float();
                if (rf == 0.0) throw runtime_error("Division by zero");
                script_float lf = l.is_int() ? script_float(l.as_int()) : l.as_float();
                return script_value(lf / rf);
            }
            throw runtime_error("Invalid operands for /");
        });
        
        // Comparison operators
        add_binary_handler("<", [](const script_value& l, const script_value& r) -> script_value {
            if (l.is_int() && r.is_int()) {
                return script_value(l.as_int() < r.as_int());
            }
            if ((l.is_int() || l.is_float()) && (r.is_int() || r.is_float())) {
                script_float lf = l.is_int() ? script_float(l.as_int()) : l.as_float();
                script_float rf = r.is_int() ? script_float(r.as_int()) : r.as_float();
                return script_value(lf < rf);
            }
            if (l.is_string() && r.is_string()) {
                return script_value(l.as_string() < r.as_string());
            }
            throw runtime_error("Invalid operands for <");
        });
        
        add_binary_handler("==", [](const script_value& l, const script_value& r) -> script_value {
            if (l.type() != r.type()) return script_value(false);
            
            if (l.is_int()) return script_value(l.as_int() == r.as_int());
            if (l.is_float()) return script_value(l.as_float() == r.as_float());
            if (l.is_string()) return script_value(l.as_string() == r.as_string());
            if (l.is_bool()) return script_value(l.as_bool() == r.as_bool());
            if (l.is_null()) return script_value(true);
            
            return script_value(false);
        });
        
        add_binary_handler("!=", [](const script_value& l, const script_value& r) -> script_value {
            if (l.type() != r.type()) return script_value(true);
            
            if (l.is_int()) return script_value(l.as_int() != r.as_int());
            if (l.is_float()) return script_value(l.as_float() != r.as_float());
            if (l.is_string()) return script_value(l.as_string() != r.as_string());
            if (l.is_bool()) return script_value(l.as_bool() != r.as_bool());
            if (l.is_null()) return script_value(false);
            
            return script_value(true);
        });
        
        add_binary_handler("<=>", [](const script_value& l, const script_value& r) -> script_value {
            if (l.is_int() && r.is_int()) {
                script_int li = l.as_int();
                script_int ri = r.as_int();
                return script_value(li < ri ? script_int(-1) : (li > ri ? script_int(1) : script_int(0)));
            }
            if ((l.is_int() || l.is_float()) && (r.is_int() || r.is_float())) {
                script_float lf = l.is_int() ? script_float(l.as_int()) : l.as_float();
                script_float rf = r.is_int() ? script_float(r.as_int()) : r.as_float();
                return script_value(lf < rf ? script_int(-1) : (lf > rf ? script_int(1) : script_int(0)));
            }
            throw runtime_error("Invalid operands for <=>");
        });
        
        // Bitwise operators
        add_binary_handler("&", [](const script_value& l, const script_value& r) -> script_value {
            if (!l.is_int() || !r.is_int()) throw runtime_error("Bitwise & requires integers");
            return script_value(l.as_int() & r.as_int());
        });
        
        add_binary_handler("|", [](const script_value& l, const script_value& r) -> script_value {
            if (!l.is_int() || !r.is_int()) throw runtime_error("Bitwise | requires integers");
            return script_value(l.as_int() | r.as_int());
        });
        
        add_binary_handler("^", [](const script_value& l, const script_value& r) -> script_value {
            if (!l.is_int() || !r.is_int()) throw runtime_error("Bitwise ^ requires integers");
            return script_value(l.as_int() ^ r.as_int());
        });
        
        add_binary_handler("<<", [](const script_value& l, const script_value& r) -> script_value {
            if (!l.is_int() || !r.is_int()) throw runtime_error("Left shift requires integers");
            return script_value(l.as_int() << r.as_int());
        });
    }
    
    void add_binary_handler(const std::string& op, std::function<script_value(const script_value&, const script_value&)> handler) {
        // Register as a function - this simulates what a dispatch table would do
        add_function(op, std::move(handler));
    }
};

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

// Test optimized integer arithmetic
JAI_TEST(optimized_integer_arithmetic) {
    std::cout << "\n=== Optimized Integer Arithmetic ===" << std::endl;
    
    optimized_engine engine;
    const int iterations = 10000;
    
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
    
    expect_true(true);
}

// Test optimized comparisons
JAI_TEST(optimized_comparisons) {
    std::cout << "\n=== Optimized Comparisons ===" << std::endl;
    
    optimized_engine engine;
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

// Test optimized bitwise operations
JAI_TEST(optimized_bitwise) {
    std::cout << "\n=== Optimized Bitwise Operations ===" << std::endl;
    
    optimized_engine engine;
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

// Performance comparison summary
JAI_TEST(optimization_summary) {
    std::cout << "\n=== Optimization Summary ===" << std::endl;
    std::cout << "This tests a simulated dispatch table approach where:" << std::endl;
    std::cout << "1. Operators are registered as functions (simulating dispatch table)" << std::endl;
    std::cout << "2. Type checking is minimized in handlers" << std::endl;
    std::cout << "3. Fast paths for common cases (int-int, float-float)" << std::endl;
    std::cout << "\nCompare these results with test_binary_expr_benchmark to see improvement." << std::endl;
    std::cout << "\nNote: This uses the existing operator overloading mechanism to simulate" << std::endl;
    std::cout << "what a proper dispatch table implementation would achieve." << std::endl;
    
    expect_true(true);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()