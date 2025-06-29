#include <iostream>
#include "../include/jaiscript/jaiscript.hpp"

int main() {
    jai::engine engine;
    
    try {
        // Test creating lambda separately
        std::cout << "Creating lambda with [=]..." << std::endl;
        engine.execute("auto lambda = [=](auto x) -> auto { return x + 1; };");
        
        std::cout << "Calling lambda..." << std::endl;
        auto result = engine.execute("lambda(5)");
        std::cout << "Result: " << result.to_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}