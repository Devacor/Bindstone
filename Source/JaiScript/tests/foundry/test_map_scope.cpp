#include <iostream>
#include <jaiscript/core/engine.hpp>

int main() {
    jai::engine eng;
    
    // Test in a single execute call
    std::cout << "=== Testing in single execute ===\n";
    try {
        auto result = eng.execute(R"(
            auto map = {};
            map["key"] = 42;
            map["key"]
        )");
        std::cout << "Result type: " << static_cast<int>(result.type()) << "\n";
        if (!result.is_null()) {
            std::cout << "Result value: " << result.as<int>() << "\n";
        } else {
            std::cout << "Result is null\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
    
    // Test persistence across execute calls
    std::cout << "\n=== Testing persistence ===\n";
    eng.execute("auto map2 = {};");
    eng.execute("map2[\"key\"] = 99;");
    
    try {
        auto result = eng.execute("map2[\"key\"]");
        std::cout << "map2[\"key\"] type: " << static_cast<int>(result.type()) << "\n";
        if (!result.is_null()) {
            std::cout << "map2[\"key\"] = " << result.as<int>() << "\n";
        } else {
            std::cout << "map2[\"key\"] is null\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
    
    return 0;
}