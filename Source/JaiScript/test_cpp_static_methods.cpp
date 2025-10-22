#include <jaiscript/jaiscript.hpp>
#include <iostream>

using namespace jai;

// Simple C++ class with static methods
class Calculator {
public:
    static int add(int a, int b) {
        return a + b;
    }
    
    static int multiply(int a, int b) {
        return a * b;
    }
    
    static int global_counter;
    
    static void increment_counter() {
        global_counter++;
    }
    
    static int get_counter() {
        return global_counter;
    }
};

int Calculator::global_counter = 0;

int main() {
    auto eng = engine::make();
    
    std::cout << "Testing C++ static method bindings...\n";
    
    try {
        // Register the Calculator class with static methods
        class_builder<Calculator>(*eng, "Calculator")
            .static_method("add", &Calculator::add)
            .static_method("multiply", &Calculator::multiply)
            .static_method("increment_counter", &Calculator::increment_counter)
            .static_method("get_counter", &Calculator::get_counter)
            .build();
        
        std::cout << "✓ Calculator class with static methods registered\n";
        
        // Test static method calls
        auto result1 = eng->execute("Calculator::add(10, 5)");
        std::cout << "✓ Calculator::add(10, 5) = " << result1.as<int>() << "\n";
        
        auto result2 = eng->execute("Calculator::multiply(6, 7)");
        std::cout << "✓ Calculator::multiply(6, 7) = " << result2.as<int>() << "\n";
        
        // Test static method with state
        auto initial_count = eng->execute("Calculator::get_counter()");
        std::cout << "✓ Initial counter = " << initial_count.as<int>() << "\n";
        
        eng->execute("Calculator::increment_counter()");
        auto updated_count = eng->execute("Calculator::get_counter()");
        std::cout << "✓ Counter after increment = " << updated_count.as<int>() << "\n";
        
        std::cout << "✅ All C++ static method tests passed!\n";
        
    } catch (const std::exception& e) {
        std::cout << "✗ Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}