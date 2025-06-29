#include <iostream>
#include "../include/jaiscript/jaiscript.hpp"

int main() {
    jai::engine engine;
    
    try {
        // Test 1: No capture
        std::cout << "Test 1: [] lambda" << std::endl;
        auto r1 = engine.execute("[](auto x) -> auto { return x + 1; }(5)");
        std::cout << "Result: " << r1.to_string() << std::endl;
        
        // Test 2: Default capture
        std::cout << "\nTest 2: [=] lambda" << std::endl;
        auto r2 = engine.execute("[=](auto x) -> auto { return x + 1; }(5)");
        std::cout << "Result: " << r2.to_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}