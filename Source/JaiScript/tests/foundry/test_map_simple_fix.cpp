#include <iostream>
#include <jaiscript/core/engine.hpp>

int main() {
    jai::engine eng;
    
    // Create a simple map
    eng.execute("auto map = {};");
    
    // Do assignment
    std::cout << "Executing: map[\"key\"] = 42;\n";
    auto result = eng.execute("map[\"key\"] = 42;");
    std::cout << "Assignment result type: " << static_cast<int>(result.type()) << "\n";
    
    // Check if we can read it back
    try {
        auto read_result = eng.execute("map[\"key\"]");
        std::cout << "Read result type: " << static_cast<int>(read_result.type()) << "\n";
        if (!read_result.is_null()) {
            std::cout << "map[\"key\"] = " << read_result.as<int>() << "\n";
        } else {
            std::cout << "map[\"key\"] is null\n";
            
            // Let's check the map size
            auto size_result = eng.execute("map.size()");
            std::cout << "map.size() = " << size_result.as<int>() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
    
    return 0;
}