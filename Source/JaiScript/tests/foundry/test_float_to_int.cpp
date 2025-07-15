#include <iostream>
#include <jaiscript/core/engine.hpp>

int main() {
    auto engine = jai::engine::make();
    
    // Test 1: Float value
    auto result = engine->execute("7.8;");
    std::cout << "Type: " << static_cast<int>(result.type()) << std::endl;
    std::cout << "Is float: " << result.is_float() << std::endl;
    std::cout << "Is int: " << result.is_int() << std::endl;
    
    try {
        int val = result.as<int>();
        std::cout << "Int value: " << val << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Error converting float to int: " << e.what() << std::endl;
    }
    
    try {
        double val = result.as<double>();
        std::cout << "Double value: " << val << std::endl;
        std::cout << "Truncated to int: " << static_cast<int>(val) << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Error getting double: " << e.what() << std::endl;
    }
    
    return 0;
}