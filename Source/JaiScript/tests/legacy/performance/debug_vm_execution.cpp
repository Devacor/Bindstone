#include <iostream>
#include "../../../include/jaiscript/core/engine.hpp"

using namespace jai;

int main() {
    try {
        // Enable debug mode
        engine vm_engine;
        vm_engine.set_backend(backend_type::jvm);
        
        // Simple cases first
        {
            std::cout << "=== Test 1: Simple literal ===\n";
            script_value result = vm_engine.execute("42;");
            std::cout << "Result type: " << static_cast<int>(result.type()) 
                      << ", is_int: " << result.is_int();
            if (result.is_int()) {
                std::cout << ", value: " << result.as_int();
            }
            std::cout << "\n\n";
        }
        
        {
            std::cout << "=== Test 2: Variable ===\n";
            script_value result = vm_engine.execute("var x = 10; x;");
            std::cout << "Result type: " << static_cast<int>(result.type()) 
                      << ", is_int: " << result.is_int();
            if (result.is_int()) {
                std::cout << ", value: " << result.as_int();
            }
            std::cout << "\n\n";
        }
        
        {
            std::cout << "=== Test 3: Simple for loop ===\n";
            script_value result = vm_engine.execute(R"(
                var sum = 0;
                for (var i = 0; i < 3; i = i + 1) {
                    sum = sum + i;
                }
                sum;
            )");
            std::cout << "Result type: " << static_cast<int>(result.type()) 
                      << ", is_int: " << result.is_int();
            if (result.is_int()) {
                std::cout << ", value: " << result.as_int();
            } else if (result.is_null()) {
                std::cout << " (NULL!)";
            }
            std::cout << "\n\n";
        }
        
        // Also test with interpreter for comparison
        {
            std::cout << "=== Test 4: Same for loop with interpreter ===\n";
            engine interp_engine;
            interp_engine.set_backend(backend_type::interpreter);
            script_value result = interp_engine.execute(R"(
                var sum = 0;
                for (var i = 0; i < 3; i = i + 1) {
                    sum = sum + i;
                }
                sum;
            )");
            std::cout << "Result type: " << static_cast<int>(result.type()) 
                      << ", is_int: " << result.is_int();
            if (result.is_int()) {
                std::cout << ", value: " << result.as_int();
            }
            std::cout << "\n\n";
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}