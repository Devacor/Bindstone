#include <iostream>
#include <jaiscript/core/engine.hpp>

using namespace jai;

int main() {
    engine eng;
    
    // First test case that fails
    std::cout << "=== Map Creation and Lookup Test ===\n";
    try {
        script_value result = eng.execute(R"(
            var map = {{"one", 1}, {"two", 2}};
            map["one"]
        )");
        std::cout << "Result: " << (result.is_null() ? "null" : result.to_string()) << "\n";
        std::cout << "Expected: 1\n";
        std::cout << "Test " << (result.is_null() ? "FAILED" : "PASSED") << "\n";
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << "\n";
    }
    
    std::cout << "\n=== Array Creation and Lookup Test (for comparison) ===\n";
    try {
        script_value result = eng.execute(R"(
            var arr = [10, 20, 30];
            arr[1]
        )");
        std::cout << "Result: " << (result.is_null() ? "null" : result.to_string()) << "\n";
        std::cout << "Expected: 20\n";
        std::cout << "Test " << (result.is_null() ? "FAILED" : "PASSED") << "\n";
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << "\n";
    }
    
    return 0;
}