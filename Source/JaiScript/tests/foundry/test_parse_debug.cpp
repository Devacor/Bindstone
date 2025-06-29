#include <iostream>
#include <jaiscript/core/engine.hpp>

int main() {
    jai::engine eng;
    
    // Simple test
    std::cout << "=== Testing simple assignment ===\n";
    eng.execute("auto x = 42;");
    eng.execute("x = 99;");
    
    std::cout << "\n=== Testing map creation and assignment ===\n";
    eng.execute("auto map = {};");
    
    std::cout << "\n=== About to do map assignment ===\n";
    try {
        std::cout << "Executing: map[\"key\"] = 123;\n";
        auto result = eng.execute("map[\"key\"] = 123;");
        std::cout << "Map assignment succeeded, result type: " << static_cast<int>(result.type()) << "\n";
        if (!result.is_null()) {
            std::cout << "Assignment returned: " << result.as<int>() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Map assignment failed: " << e.what() << "\n";
    }
    
    std::cout << "\n=== Reading from map ===\n";
    try {
        auto result = eng.execute("map[\"key\"]");
        std::cout << "map[\"key\"] = " << result.as<int>() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Failed to read from map: " << e.what() << "\n";
    }
    
    return 0;
}