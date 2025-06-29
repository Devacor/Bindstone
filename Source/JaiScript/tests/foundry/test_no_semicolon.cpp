#include <iostream>
#include <jaiscript/core/engine.hpp>

int main() {
    jai::engine eng;
    
    // Create map
    eng.execute("auto map = {}");
    
    // Try assignment without semicolon
    std::cout << "=== Without semicolon ===\n";
    try {
        auto result = eng.execute("map[\"key\"] = 42");
        std::cout << "Success! Result type: " << static_cast<int>(result.type()) << "\n";
        if (result.is_int()) {
            std::cout << "Assignment returned: " << result.as<int>() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
    
    // Check if it worked
    try {
        auto read = eng.execute("map[\"key\"]");
        std::cout << "Read back type: " << static_cast<int>(read.type()) << "\n";
        if (read.is_int()) {
            std::cout << "map[\"key\"] = " << read.as<int>() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Read error: " << e.what() << "\n";
    }
    
    return 0;
}