#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    
    // Test lambda with typed parameters
    try {
        auto result = engine.execute(R"(
            var multiply = [](int a, int b) -> int { return a * b; };
            multiply(5, 6);
        )");
        std::cout << "Lambda typed params SUCCESS: " << result.as_int() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Lambda typed params FAILED: " << e.what() << std::endl;
    }
    
    // Test lambda with colon parameters
    try {
        auto result = engine.execute(R"(
            var multiply = [](:a, :b) -> { return a * b; };
            multiply(5, 6);
        )");
        std::cout << "Lambda colon params SUCCESS: " << result.as_int() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Lambda colon params FAILED: " << e.what() << std::endl;
    }
    
    // Test lambda with auto parameters
    try {
        auto result = engine.execute(R"(
            var multiply = [](auto a, auto b) -> { return a * b; };
            multiply(5, 6);
        )");
        std::cout << "Lambda auto params SUCCESS: " << result.as_int() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Lambda auto params FAILED: " << e.what() << std::endl;
    }
    
    // Test lambda without parameter types (should this work?)
    try {
        auto result = engine.execute(R"(
            var multiply = [](a, b) -> { return a * b; };
            multiply(5, 6);
        )");
        std::cout << "Lambda untyped params SUCCESS: " << result.as_int() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Lambda untyped params FAILED: " << e.what() << std::endl;
    }
    
    return 0;
}
