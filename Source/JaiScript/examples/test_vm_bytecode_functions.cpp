#include <iostream>
#include <chrono>
#include "jaiscript/jvm/vm_function_integration.hpp"
#include "jaiscript/jvm/vm_class_system.hpp"

using namespace jaiscript;
using namespace jaiscript::jvm;

// Simple test to verify functions are compiled to bytecode
int main() {
    std::cout << "Testing VM Bytecode Function Compilation\n";
    std::cout << "=======================================\n\n";
    
    try {
        // Create VM-aware engine
        vm_aware_engine engine;
        
        // Enable profiling to track execution
        g_function_profiler.start_profiling();
        
        // Test 1: Basic function compilation
        std::cout << "Test 1: Basic Function to Bytecode\n";
        engine.execute(R"(
            auto add(auto x, auto y) -> auto {
                print("add() executing in: BYTECODE");
                return x + y;
            }
        )");
        
        // Verify function is in VM registry
        auto& registry = engine.get_function_registry();
        auto func = registry.find_function("add");
        if (func && !func->bytecode.empty()) {
            std::cout << "✓ Function 'add' compiled to " << func->bytecode.size() << " bytes of bytecode\n";
        } else {
            std::cout << "✗ Function 'add' NOT compiled to bytecode!\n";
        }
        
        // Test 2: Execute and verify bytecode execution
        std::cout << "\nTest 2: Bytecode Execution\n";
        auto result = engine.execute("add(5, 3)");
        std::cout << "Result: " << result.as<script_int>() << "\n";
        
        // Test 3: Bytecode-to-bytecode calls
        std::cout << "\nTest 3: Bytecode-to-Bytecode Calls\n";
        engine.execute(R"(
            auto multiply(auto x, auto y) -> auto {
                print("multiply() executing in: BYTECODE");
                return x * y;
            }
            
            auto calculate(auto a, auto b) -> auto {
                print("calculate() executing in: BYTECODE");
                // These should be bytecode-to-bytecode calls
                auto sum = add(a, b);
                auto product = multiply(a, b);
                return sum + product;
            }
        )");
        
        result = engine.execute("calculate(4, 6)");
        std::cout << "Result: " << result.as<script_int>() << " (should be 10 + 24 = 34)\n";
        
        // Stop profiling and check results
        g_function_profiler.stop_profiling();
        
        // Verify all functions executed as bytecode
        std::cout << "\nExecution Statistics:\n";
        auto profiles = g_function_profiler.get_hot_functions(0);
        
        bool all_bytecode = true;
        for (const auto& profile : profiles) {
            std::cout << "Function '" << profile.name << "':\n";
            std::cout << "  Bytecode calls: " << profile.bytecode_calls << "\n";
            std::cout << "  Interpreter fallbacks: " << profile.interpreter_fallbacks << "\n";
            
            if (profile.interpreter_fallbacks > 0) {
                all_bytecode = false;
            }
        }
        
        std::cout << "\n" << (all_bytecode ? "✓ SUCCESS: All functions executed as bytecode!" 
                                           : "✗ FAILURE: Some functions fell back to interpreter!") << "\n";
        
        // Test 4: Performance comparison
        std::cout << "\nTest 4: Performance Comparison\n";
        
        // Define computation-heavy function
        engine.execute(R"(
            auto fibonacci(auto n) -> auto {
                if (n <= 1) return n;
                return fibonacci(n - 1) + fibonacci(n - 2);
            }
        )");
        
        // Time execution
        auto start = std::chrono::high_resolution_clock::now();
        result = engine.execute("fibonacci(25)");
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::cout << "Fibonacci(25) = " << result.as<script_int>() << "\n";
        std::cout << "Execution time: " << duration << " ms\n";
        
        // Test 5: Class method bytecode
        std::cout << "\nTest 5: Class Methods as Bytecode\n";
        
        auto vm_classes = enable_vm_class_system(
            engine.get_vm_executor(),
            engine.get_vm_compiler()
        );
        
        engine.execute(R"(
            class TestClass {
                int compute(int x) {
                    print("TestClass::compute() executing in: BYTECODE");
                    return x * 2;
                }
            }
            
            auto obj = new TestClass();
            obj.compute(21)
        )");
        
        std::cout << "\nAll tests completed!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}