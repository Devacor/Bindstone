#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <chrono>
#include <iomanip>

using namespace jai;
using namespace std::chrono;

void benchmark_script(const std::string& name, const std::string& script, int iterations = 100) {
    std::cout << "\n" << name << " (Script size: " << script.size() << " chars)\n";
    std::cout << std::string(60, '-') << "\n";
    
    // Test with auto-selection
    {
        engine engine;
        // Default is auto_select
        
        auto start = high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            engine.execute(script);
        }
        auto end = high_resolution_clock::now();
        
        auto duration = duration_cast<microseconds>(end - start).count();
        double avg = static_cast<double>(duration) / iterations;
        
        backend_type selected = (script.size() > 1000) ? backend_type::jvm : backend_type::interpreter;
        std::cout << "Auto-selected: " << (selected == backend_type::jvm ? "VM" : "Interpreter") 
                  << " - " << std::fixed << std::setprecision(2) << avg << " μs/iteration\n";
    }
    
    // Force interpreter
    {
        engine engine;
        engine.set_backend(backend_type::interpreter);
        
        auto start = high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            engine.execute(script);
        }
        auto end = high_resolution_clock::now();
        
        auto duration = duration_cast<microseconds>(end - start).count();
        double avg = static_cast<double>(duration) / iterations;
        
        std::cout << "Interpreter:   " << std::fixed << std::setprecision(2) 
                  << avg << " μs/iteration\n";
    }
    
    // Force VM
    {
        engine engine;
        engine.set_backend(backend_type::jvm);
        
        auto start = high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            engine.execute(script);
        }
        auto end = high_resolution_clock::now();
        
        auto duration = duration_cast<microseconds>(end - start).count();
        double avg = static_cast<double>(duration) / iterations;
        
        std::cout << "VM:            " << std::fixed << std::setprecision(2) 
                  << avg << " μs/iteration\n";
    }
}

int main() {
    std::cout << "╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║     JaiScript Hybrid Execution Model Demo             ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n";
    std::cout << "\nAuto-selection threshold: 1000 characters\n";
    
    // Small script (< 1000 chars) - should use interpreter
    std::string small_script = R"(
        auto x = 10;
        auto y = 20;
        x + y
    )";
    
    // Medium script (~500 chars) - still uses interpreter
    std::string medium_script = R"(
        auto fibonacci(auto n) -> auto {
            if (n <= 1) return n;
            return fibonacci(n-1) + fibonacci(n-2);
        }
        
        auto factorial(auto n) -> auto {
            if (n <= 1) return 1;
            return n * factorial(n-1);
        }
        
        auto sum_range(auto start, auto end) -> auto {
            auto sum = 0;
            for (auto i = start; i <= end; i = i + 1) {
                sum = sum + i;
            }
            return sum;
        }
        
        fibonacci(10) + factorial(5) + sum_range(1, 100)
    )";
    
    // Large script (> 1000 chars) - should use VM
    std::string large_script = R"(
        // Matrix operations - computationally intensive but not deeply recursive
        auto create_matrix(auto rows, auto cols, auto initial_value) -> auto {
            auto matrix = [];
            for (auto i = 0; i < rows; i = i + 1) {
                auto row = [];
                for (auto j = 0; j < cols; j = j + 1) {
                    row.push(initial_value);
                }
                matrix.push(row);
            }
            return matrix;
        }
        
        auto matrix_multiply(auto a, auto b, auto size) -> auto {
            auto result = create_matrix(size, size, 0);
            
            for (auto i = 0; i < size; i = i + 1) {
                for (auto j = 0; j < size; j = j + 1) {
                    auto sum = 0;
                    for (auto k = 0; k < size; k = k + 1) {
                        sum = sum + a[i][k] * b[k][j];
                    }
                    result[i][j] = sum;
                }
            }
            
            return result;
        }
        
        // Calculate trace of a matrix (sum of diagonal elements)
        auto matrix_trace(auto matrix, auto size) -> auto {
            auto trace = 0;
            for (auto i = 0; i < size; i = i + 1) {
                trace = trace + matrix[i][i];
            }
            return trace;
        }
        
        // Create test matrices
        auto size = 4;
        auto matrix_a = create_matrix(size, size, 2);
        auto matrix_b = create_matrix(size, size, 3);
        
        // Fill with some values
        for (auto i = 0; i < size; i = i + 1) {
            for (auto j = 0; j < size; j = j + 1) {
                matrix_a[i][j] = i + j + 1;
                matrix_b[i][j] = i * j + 1;
            }
        }
        
        // Multiply matrices and calculate trace
        auto result_matrix = matrix_multiply(matrix_a, matrix_b, size);
        auto trace_value = matrix_trace(result_matrix, size);
        
        trace_value  // Return the trace
    )";
    
    benchmark_script("Small Script", small_script, 1000);
    benchmark_script("Medium Script", medium_script, 500);
    benchmark_script("Large Script", large_script, 100);
    
    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║                 Hybrid Strategy Benefits              ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n";
    std::cout << "• Small scripts use interpreter (no compilation overhead)\n";
    std::cout << "• Large scripts use VM (better performance for complex code)\n";
    std::cout << "• Automatic selection based on script size\n";
    std::cout << "• Can override with set_backend() when needed\n";
    
    // Demo: Single engine with mixed execution
    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║              Mixed Execution in Same Engine           ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n";
    
    engine engine;
    // Use auto-select mode (default)
    
    std::cout << "\nExecuting small expression (auto-selects interpreter):\n";
    auto result1 = engine.execute("2 + 2");
    std::cout << "Result: " << result1.as<script_int>() << "\n";
    
    std::cout << "\nExecuting large script (auto-selects VM):\n";
    auto result2 = engine.execute(large_script);
    std::cout << "Result: " << result2.as<script_int>() << "\n";
    
    std::cout << "\nBoth executions share the same global environment!\n";
    engine.add_global("shared_value", script_value(42));
    
    auto result3 = engine.execute("shared_value * 2");  // Small - uses interpreter
    std::cout << "Small script result: " << result3.as<script_int>() << "\n";
    
    // Create a large script that uses the shared value
    std::string use_shared = R"(
        // Large script to trigger VM usage
        auto compute_with_shared(auto shared) -> auto {
            auto result = 0;
            for (auto i = 0; i < 100; i = i + 1) {
                result = result + i;
            }
            return result + shared;
        }
        
        // Lots of padding to exceed 1000 chars threshold
        auto dummy1 = 1; auto dummy2 = 2; auto dummy3 = 3; auto dummy4 = 4;
        auto dummy5 = 5; auto dummy6 = 6; auto dummy7 = 7; auto dummy8 = 8;
        auto dummy9 = 9; auto dummy10 = 10; auto dummy11 = 11; auto dummy12 = 12;
        auto dummy13 = 13; auto dummy14 = 14; auto dummy15 = 15; auto dummy16 = 16;
        auto dummy17 = 17; auto dummy18 = 18; auto dummy19 = 19; auto dummy20 = 20;
        
        compute_with_shared(shared_value)
    )";
    auto result4 = engine.execute(use_shared);
    std::cout << "Large script result: " << result4.as<script_int>() << "\n";
    
    return 0;
}